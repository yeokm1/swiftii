# 003 - Apple II Plus typing model

This doc describes the current typing and display model for Swift source on
Apple II machines. The durable rules are:

- `.swift` files store canonical lowercase ASCII, independent of the authoring
  machine.
- The disk declares the target machine; //e passthrough is a build-time
  `WITH_IIE` choice, not a runtime machine-ID probe. `$FBB3` is unreliable
  across emulators and with the language card banked in.
- On pre-IIe displays, lowercase renders as normal uppercase and uppercase as
  inverse uppercase. Stored bytes do not change.
- Plain text files are exempt: the editor and file-browser preview treat
  non-`.swift` names as verbatim native text.

## Architecture

Three layers, each with its own rule:

1. **Files** - canonical lowercase ASCII. Byte-identical across
   machines. Non-negotiable.
2. **Input** - machine-dependent transform at typing time. On //+
   (and the original Apple II): auto-lowercase letters; apostrophe
   case markers; Ctrl-W for `_`; C-standard digraphs. On //e and
   later: passthrough (the keyboard is already canonical) - **but only
   in a `WITH_IIE` build** (the //e-disk binaries); the
   II+-disk binaries keep the //+ transform on every machine.
3. **Display** - machine-dependent rendering. On pre-IIe: uppercase
   → inverse-video uppercase, lowercase → normal-video uppercase;
   `{` `}` `|` → digraph forms (no glyph
   in the char ROM). On //e and later: direct `$E0..$FF` screen-code
   write (cc65's `cputc` folds lowercase) - again **only in
   a `WITH_IIE` build** (the //e-disk binaries); the II+-disk binaries
   keep the pre-IIe path (and so show caps for lowercase on a //e).

The lexer, compiler, and VM see canonical bytes regardless of
platform.

```
                              //+                  //e and later
                              ───                  ─────────────
keyboard scancodes      →  keyboard.c              keyboard.c
canonical byte stream   ←  input.c (transform)     (passthrough)
                            │
                            ├─→ repl.c
                            └─→ editor.c           (Phase 14)
storage / lexer feed    ←  canonical lowercase ASCII (both)
```

## Typing model (//+ side)

| Want | Type | Notes |
|------|------|-------|
| lowercase letter | letter key | the default |
| single uppercase letter | `'` then letter | `'` applies to next letter only |
| run of uppercase letters | `''` then letters | runs until first non-letter |
| literal `'` in a string between letters | `'` between two letters | contextual rule, see below |
| `_` | Ctrl-W (`$17`) | input-layer keystroke (same key as the editor) |
| `{` `}` `[` `]` `\` `\|` | digraph table below | translated at input |

### Apostrophe inside strings

Inside `"..."`, the apostrophe is contextual:

- Between two letters: literal apostrophe (`"DON'T"` → `"don't"`).
- After a non-letter (including the opening `"`): case marker
  (`"'HELLO"` → `"Hello"`).

### Apostrophe inside comments

In `//` line comments and `/* */` block comments, auto-lowercase
still applies but case markers and digraph sequences pass through
literally. A comment can document the typing model without itself
being mangled.

## Digraph table

| Want | Type | Source |
|------|------|--------|
| `{`  | `<%`  | C++ digraph |
| `}`  | `%>`  | C++ digraph |
| `[`  | `<:`  | C++ digraph |
| `]`  | `:>`  | C++ digraph |
| `\`  | `??/` | C trigraph |
| `\|` | `??!` | C trigraph (reserved; SwiftII Tier 1 doesn't use `\|`) |

Digraphs translate inside strings too - required so `??/` delivers
`\` for `\n`, `\t`, `\(...)`, etc.

## Display layer (pre-IIe)

The pre-IIe character ROM has glyphs for ASCII `$20`–`$5F` only.
Bytes outside that range are substituted at `emit()`:

| Byte | Rendering on //+ |
|------|------------------|
| `A`–`Z` | **inverse-video uppercase** (visual case indicator) |
| `a`–`z` | normal-video uppercase |
| `{`  | `<%` |
| `}`  | `%>` |
| `\|` | `??!` |
| backtick, tilde | reserved for future substitution |

Bytes stored in memory are unchanged. `s.count` on `"hello{}"`
returns 7 on every machine; only the //+ display width is two
columns longer per `{` or `}`. IIe and later render natively.

The inverse-video conversion uses the cc65 conio inverse set at
screen indices `$01`–`$1A`: subtract `$40` from the uppercase byte
before `cputc`. `A` → `$01`, `B` → `$02`, …, `Z` → `$1A` - all
land in the inverse-uppercase glyph range. Lowercase is uppercased
(`c - $20`) and emitted as ordinary normal-video text.

## Plain text files are exempt (editor + file browser)

The whole model above - the typing transforms **and** the pre-IIe
display layer (inverse-video case swap + `{ } |` digraphs) - is a
**Swift-source** convention. It applies only to a `.swift` file. A
plain text file (any non-`.swift` name, e.g. the on-disk
`README.TXT` help) is treated as plain text on every machine:

- **Editor load/save**: byte-for-byte verbatim. No `input_translate`
  / `input_untranslate`, so no auto-lowercase, no `'` case markers,
  no digraph collapse. Capitals and literal `<% | { }` survive a
  round trip unchanged.
- **Editor + file-browser preview display (pre-IIe)**: native. No
  inverse-video case swap and no digraph expansion - bytes `$20`–`$5F`
  render as-is. So an **all-caps** text file reads as plain
  normal-video text on a //+, exactly as the launcher's old built-in
  help screen did. (A `.swift` file keeps the inverse/digraph
  rendering of the previous section.)

This is why the II+ disks ship an **all-caps** `README.TXT` while the
//e disks (native lowercase) ship a mixed-case one. The gate is purely
the filename: `editor_path_is_swift` (`src/editor/fileio.c`) for the
editor, `row_is_swift` (`tools/apple2/boot_launcher/boot_launcher.c`) for the
browser preview. An untitled scratch buffer counts as Swift (the
editor's primary use). The interpreters' REPL keyboard is unaffected -
it only ever feeds Swift, so `input_translate` there is unconditional
and byte-identical to before.

## Raw input status

The original design included a `:raw` REPL meta-command and a persistent
`input_raw_mode` flag. The 2026-05-23 budget sweep removed both: the II+ REPL
input layer is now always on, and no live code path disables
`input_translate`. Raw editing still exists in the launcher editor via its
file-mode `s_cooked` flag; on a II+ editor build that mode is toggled with
Ctrl-G, not a REPL meta-command.

## Implementation map

| File | Role |
|------|------|
| `src/platform/apple2/input.c` / `.h` | The state machine. Portable C; linked into the host and test binaries too so unit tests can drive it directly. |
| `src/platform/apple2/keyboard.c` | Calls `input_translate` once at Return, after the typed line is fully buffered. Lets Ctrl-W through the control-char filter and echoes `_`. |
| `src/platform/apple2/screen.c` | `emit()` runs the pre-IIe display substitution table: lowercase → normal uppercase, capitals → inverse uppercase. |
| `src/repl/repl.c` | Receives canonical bytes from `platform_read_line`; no extra translation. |
| `src/repl/metacmds.c` | REPL meta-commands (`:help`, `:list`, `:mem`, `:reset`, `:quit`). |
| `src/file_runner/file_runner.c` | Reads `.SWIFT` files as-is; canonical on disk by invariant. |
| `tests/unit/input_translate_test.c` | Unit tests covering case markers, Ctrl-W, digraphs, string apostrophe, comments, edge cases, idempotence. |

The translator is a **batch function** (not per-keystroke): one
call per Return-terminated line. This avoids partial-keystroke echo bugs and
mirrors how the editor rewrites the gap buffer at save time.
Per-keystroke variants are deferred until the editor needs them.

**The `_` key is `Ctrl-W` (`$17`) in both the REPL and the editor.** The II+
keyboard has no `_` key, so the input method supplies one. It was originally
`Ctrl-U` (= the right-arrow byte `$15`), but that surprised users ("right-arrow
types underscore") and collided with the editor's non-destructive cursor
motion, so both the REPL and the editor settled on `Ctrl-W` (the editor maps it
per-key in `editor_cook_key`; the REPL maps it in batch here). Right-arrow
(`$15`) is now inert at the REPL prompt. See [design/006](006-editor.md).

## Cost

The typing model costs roughly +1,800 bytes CODE - more than the
back-of-envelope C-line count implied, because cc65 expands the
state-machine `switch` cascade and the multiple `emit_char` call sites in
`input_translate`. BSS impact is negligible: there is no persistent translator
state - the batch translator keeps all state on its own stack frame.

## Lessons (post-shipment)

- **String-escape auto-lowercase.** Initial draft passed the byte
  after `\` through verbatim. On //+ that meant `\N` reached the
  lexer as `\N`, violating the canonical-lowercase invariant.
  Fixed: the escape-consume path now auto-lowercases letters but
  still bypasses the string state-toggle on `\"` and the
  apostrophe rule. Caught by `test_input_string_escape_passthrough`.
- **Run marker terminates at non-letters.** Initial test expected
  `''ABC d` to collapse to `ABCd` - wrong; the space ends the run
  and survives to the output (`ABC d`). Spec was always correct;
  test was wrong.
- **Block comments inside header docstrings.** clang's -Wcomment
  rejects `/* */` embedded inside a `/* ... */` block. The input.h
  rule list had to drop the literal sequence; documented as prose
  instead.
- **Pre-IIe inverse-video conversion.** cc65's conio stores
  normal-video bytes as `ch ^ $80` and inverse-video bytes as
  `ch & $3F`. The simplest way to render an uppercase letter as
  inverse uppercase is `cputc(c - 0x40)` - `A` → `$01`, which is
  the inverse-uppercase `A` glyph. Lowercase is uppercased and
  drawn normal-video. No new screen mode, no font swap.
- **`J`/`M` collide with cputc's CR/LF.** Two of those inverse
  screen codes are control characters `cputc` intercepts: `J` →
  `$0A` (LF) and `M` → `$0D` (CR). An uppercase `J`/`M` - common in
  ProDOS file names - then fires a stray newline mid-word and wraps
  the rest of the line back over itself (`ENAMELEN` → `ELEN`). The
  fix writes those two letters straight to video RAM and steps the
  cursor by hand (`emit_inverse_letter`, like `emit_native_high` on
  //e), gated `WITH_INVERSE_JM`. It is on for every pre-IIe (II+/
  Saturn) binary that echoes uppercase text: the **Runner** (program
  output + its `Running:` echo) and the **pre-IIe Compilers** (their
  `Compiling:` / `Wrote:` ProDOS-path echo). The ~80 B of `-Cl` static
  BSS is paid on the II+ Runner by trimming `HEAP_SIZE` 2176 → 2112
  and fits the at-budget Compilers (II+ ~8 B, Saturn ~29 B margin);
  the at-ceiling REPLs cannot absorb it and stay byte-identical (they
  have no uppercase path echo). The direct write must use `ch ^ $80`
  - not the raw inverse code - so `J`/`M` get the same polarity as the
  letters around them (`cputc` stores `(ch ^ $80) & INVFLG`, and with
  the default `INVFLG = $FF` those render normal). The **//e** Compiler
  does not use this branch at all: like the rest of the //e disk it
  builds `WITH_IIE` (full-ASCII `emit_native_high`/`cputc`, no
  `emit_inverse_letter`); built without it, it falls into the pre-IIe
  runtime-probe path and garbles the same way. See LESSONS.md,
  "Inverse-video `J` and `M` collide with cputc's CR/LF".
- **Echo timing on `'`.** Batch translation sidesteps the question of
  whether a `'` should echo immediately (and backspace when the letter
  resolves) or stay suppressed until the letter arrives: the screen
  shows what the user typed during entry, then the line is rewritten in
  place before hand-off. The visual mismatch between echo and stored
  byte is documented in the user-facing guide (LANGUAGE.md "Typing on
  Apple II Plus").

## References

- User-facing description: [`docs/using/LANGUAGE.md` section Typing on Apple II
  Plus](../../using/LANGUAGE.md).
- Machine-type detection (cached at `platform_init`):
  `src/platform/apple2/osdetect.{c,h}`. The `$FBB3` probe per Apple
  Technical Note #7 - value `$38` for original Apple II and II+.
