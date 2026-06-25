# 005 - REPL design

SwiftII ships a minimal REPL: a plain `> ` prompt, implicit print of
bare expressions, persistent globals/functions, and five meta-commands.
The earlier Swift-LLDB-style polish layer (numbered prompts,
`name: Type = value` echoes, `$R<n>` auto-results, `OP_REPL_ECHO`) was
removed to fund higher-priority language work.

## Problem

The REPL is a primary way to use SwiftII: the boot launcher's first
menu option drops the user into it. The REPL must share the lexer,
compiler, and VM with file mode (a separate front-end would double
the binary footprint), accept input one line at a time, and keep
globals and function definitions alive across lines so a `let x = 1`
on one line is visible to `print(x)` on the next.

## Proposal

The shipped REPL is intentionally minimal:

- **Banner** at startup: version + build date, copyright, hint line
  pointing at `:help` and `:quit`.
- **Prompt**: plain `> ` (two characters), no numbering.
- **Input**: one line at a time via `platform_read_line` into a
  fixed line buffer.
- **Meta-commands** (prefix `:`, handled before lexing): `:help`,
  `:list`, `:mem`, `:reset`, `:quit`. Non-`:` input is forwarded to
  the compiler.
- **Compile**: REPL mode calls `compiler_compile_source_repl`, which
  emits to the top-level scratch region of the shared bytecode
  buffer (`bcbuf`); function bodies rotate into the persistent
  arena and stay callable across inputs.
- **Implicit print**: bare top-level expressions in REPL mode are
  auto-wrapped in `print(value)` so `1 + 2` yields `3` (BASIC /
  Python style). File mode discards bare-expression values via
  `OP_POP`. The split is keyed off `compiler_compile_source_repl`;
  see `emit_expr_stmt_end` in `src/compiler/statements.c`.
- **`let` / `var` / assignment**: silent (no echo).
- **Globals + function table**: persist across lines. `globals_reset`
  + `vm_reset_globals` + `heap_reset` run once at REPL startup and
  again on `:reset`.

REPL-only; file mode is identical to a no-REPL build.

## Detailed design

### Driver

`repl_run` (in `src/repl/repl.c`) is a one-function loop:

1. `globals_reset` + `vm_reset_globals` + `heap_reset` (clean state).
2. Print banner.
3. Forever: print `> `, read a line, dispatch meta-commands first,
   else call `compile_impl` then `vm_run`.

There is no input-counter, no `$R<n>` machinery, and no `repl_print_top`
flag in the parser. Errors print a single line and continue the loop.

### Meta-commands (`src/repl/metacmds.c`)

| Command  | Effect                                                      |
|----------|-------------------------------------------------------------|
| `:help`  | Prints the single line `:help :list :mem :reset :quit`. The //+ digraph guide lives in `LANGUAGE.md` to keep RODATA small. |
| `:list`  | Iterates `globals_count()` in definition order, printing each global as `let name: Type = value` or `var name: Type = value`. |
| `:mem`   | Prints heap-used / heap-free bytes via `heap_used()` / `heap_free_bytes()`. |
| `:reset` | Calls `globals_reset` + `vm_reset_globals` + `heap_reset`. The function arena (`bcbuf` watermark) is reset via `funcs_reset` → `bcbuf_arena_reset`. |
| `:quit`  | Returns `METACMD_QUIT`; `repl_run` exits. |

Meta-commands match by exact string equality on the line content.
Any other `:`-prefixed input falls through to `:help`'s default
fallthrough.

### What the REPL deliberately does **not** do today

These were considered, designed in earlier revisions of this doc,
and either dropped or deferred:

- **Numbered prompts** (`  1> `, `  2> `, ...) - dropped.
- **`name: Type = value` binding echo** - dropped. The
  `OP_REPL_ECHO` opcode (was `$E0`) is no longer allocated; see
  `OPCODES.md` (its slot is free).
- **`$R<n>` auto-result naming** - dropped. The lexer no
  longer has a `$` branch; `$` is illegal everywhere.
- **Multi-line input** - not in the REPL; the in-launcher editor is the
  home for block-level authoring. Each REPL input must be a single
  complete statement.

If the dropped polish-layer features come back, they need a fresh design
doc under a new number - that layer was reverted whole-cloth, not just
feature-gated.

**Function redefinition** is the one item that did come back: the //e
binaries (SWIFTIIE / SWIFTAUX) rebind a redefined `func` and print a
`redef foo` notice (shipped Phase 16); the II+ binaries (SWIFTIIP /
SWIFTSAT) still reject it with `too many funcs` - they sit at the
ProDOS file ceiling - so `:reset` stays the recovery path there.

## Alternatives considered (current scope)

1. **Forward `:` lines into the compiler as no-ops.** Considered for
   the minimal version (no meta-command system at all). Rejected:
   `:reset` is the only way to recover from a full function table or
   a corrupted heap, so it must exist.

2. **Auto-print bare expressions via an explicit `OP_AUTO_PRINT`
   opcode** instead of emitting `BUILTIN_PRINT` at compile time.
   Rejected: the print path already handles every value type; an
   explicit opcode would duplicate the dispatch with no payoff.

(The polish-layer alternatives - separate `$R` table, echo via
`BUILTIN_PRINT` wrapper, savepoint-as-struct - are preserved in git
history and not relevant to the current scope.)

## Cost

The current minimal REPL costs roughly what the meta-command system
and the banner string cost (single-digit hundreds of bytes of CODE
plus the strings in RODATA / LC). Implicit print of bare expressions
is a one-line emit in `emit_expr_stmt_end`, no opcode cost.

## Decision

The full polish layer was unwound and the opcode-table slot for
`OP_REPL_ECHO` was vacated. Future ambitions for a richer REPL surface live
in ROADMAP "Maybe / probably never" item 26 and need a fresh design
proposal.
