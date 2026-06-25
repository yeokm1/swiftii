# 004 - Demo-oriented Feature Scope

## Problem

The roadmap as of Phase 3 close has grown by accretion. Each feature
was justified individually but the set as a whole was never audited
against the actual audience: a retrocomputing enthusiast writing demos
on real or emulated Apple II hardware. There is no production use case
for SwiftII. Three distinct problems result.

**1. Over-scoped features that won't be used.** Phase 7 (Tier 2)
includes `guard let`, `assert`, and the `hasPrefix`/`hasSuffix` string
methods. Each has negligible value for the demo audience: `assert` has
zero use in untested demo code; `guard let` saves one level of
indentation that small demos rarely have to begin with;
`hasPrefix`/`hasSuffix` matter for text-parsing utilities that are not
what people write on a 1 MHz machine.

**2. Under-scoped features that hurt demo viability.** Two omissions
seriously limit what can be written, in ways Applesoft and Pascal do
not suffer from:

- **No `break` statement.** Every game loop, every search loop, every
  "wait for input" loop benefits from early exit. Applesoft has `GOTO`;
  Pascal has `EXIT`; SwiftII has nothing. The workaround - a `running`
  flag checked at the top of each loop - is uglier than the Applesoft
  equivalent and makes nested loops painful.
- **`print()` always adds a newline.** Applesoft's `PRINT A; B; C` and
  `PRINT "prompt"; : INPUT A` patterns have no SwiftII equivalent.
  Demos cannot build up a formatted line (table rows, progress dots,
  status bars) or print an inline prompt before `readLine()`.

**3. Phase 4 implementation limits prevent any meaningful demo on
target.** The 512-byte source-file cap means FizzBuzz fits but a text
adventure room description doesn't. The 8-function cap means a
well-structured GR demo can't be split into helpers. The 7-character
identifier limit silently collides realistic Swift-style variable
names (`balance` and `balance1` both truncate to `balance`; `position`
truncates to `positio`). None of these limits are language-design
choices - they are temporary BSS-budget caps that gate whether the
language can show off what it does on real hardware.

> The Phase-3 caps this section argued against have since been raised
> (identifiers 7 → 12 chars; source is disk-bounded under the Family B
> compiler), and an **over-length identifier is now a compile-time
> error**, not a silent truncation/collision - see
> [LANGUAGE.md → Implementation limits](../../using/LANGUAGE.md#implementation-limits).

## Proposal

**Add to Tier 1** (deliver in Phase 4 alongside functions and
optionals - both additions are small):

- `break` statement inside `while` and `for-in` loops. No new VM
  mechanics: compiles to an unconditional jump using the existing
  `OP_JUMP`, patched to the loop-end address.
- `print(_:terminator:)` builtin overload -
  `print("prompt: ", terminator: "")` prints without trailing newline.
  No grammar change required; the `terminator:` form is a labelled
  call that the compiler recognises as a second print entry point.

**Raise Phase 4 implementation limits** to demo-usable levels:

| Limit                              | Current Phase 4 | Proposed |
|------------------------------------|-----------------|----------|
| Source file (Apple II)             | 512 B           | 2 KB     |
| Compiled bytecode (Apple II)       | 256 B           | 1 KB     |
| Functions (per program)            | 8               | 16       |
| Globals (per program)              | 16              | 32       |
| Locals per function                | 8               | 16       |
| Identifier storage                 | 7 chars         | 12 chars |

The 8-KB Phase 6+ targets still apply. This is a stepping stone - it
raises every cap by 2×–4× without requiring the Phase 6 LC migration.

**Drop from Phase 7** (move to `Maybe / probably never`):

- `assert(_:)` - zero use in demo code; not in Applesoft or Pascal.
- `hasPrefix` / `hasSuffix` string methods - text-parsing utilities
  are not the target use case.

**Defer to end of Phase 7** (implement last; cut entirely if budget
runs out):

- `guard let` - `if let ... else { return }` covers every realistic
  demo use; the indentation savings aren't worth a separate parser
  branch on this codebase.

## Detailed design

### `break`

Grammar addition:

```
statement   ::= ... | break_stmt
break_stmt  ::= 'break'
```

`break` is added to the Tier 1 reserved word list. The parser emits
`OP_JUMP` with a backpatch slot when it encounters `break`; the loop
compiler maintains a per-loop stack of pending break sites. When the
loop body closes, all collected sites are patched to point past the
loop. `break` outside a loop is a compile error.

`continue` is **not** proposed at this time. Most demo loops that
would benefit from `continue` are equally well written with an `if`
that guards the rest of the body. Revisit if real demo code starts
relying on the workaround.

### `print(_:terminator:)`

The Tier 1 `print(_:)` already compiles to a print opcode that emits
the value followed by `\n`. Add a second compiler entry point for the
`terminator:` form. When the call has a `terminator:` argument that is
a **string literal**, emit the value followed by the literal bytes
(via the existing string-emit path). Only string-literal terminators
are supported in v1 - runtime-computed terminators would require a
new opcode and runtime branch on terminator length, which is not worth
the complexity for the demo use case.

Common terminators in practice:
- `terminator: ""` - no trailing character (the prompt case)
- `terminator: " "` - space (table rows)
- `terminator: "\t"` - tab

The default (no `terminator:` argument) remains `"\n"`.

### Phase 4 limit raises

`src/common/config.h` constants change as follows:

```c
#define IDENT_MAX     12      // was 7
#define MAX_GLOBALS   32      // was 16
#define MAX_FUNCS     16      // was 8
#define MAX_LOCALS    16      // was 8
#define FILE_SRC_SIZE 2048    // was 512
#define FILE_BC_SIZE  1024    // was 256
```

`MEMORY_MAP.md` Phase 3 actual layout BSS description is updated to
reflect the new sizes once these constants land.

## Alternatives considered

**Do nothing - demo on host only.** Treat the target build as a tech
demo of the interpreter and accept that real Apple II hardware can't
run anything past FizzBuzz until Phase 6. Rejected because the
project's explicit goal includes "tested on at least one real Apple II
Plus" (ROADMAP Phase 17), and shipping a v1 that can't run a real demo
on real hardware defeats the point.

**Raise limits more aggressively (4 KB source, 32 functions, 64
globals).** Rejected because the BSS delta would crowd the cc65
C-stack reserve and shrink headroom for Phase 4's own additions. The
proposed values give 2×–4× headroom over FizzBuzz-class demos while
leaving margin for Phase 4 (function call stack, frame pointer
storage, array machinery).

**Keep `assert` / `hasPrefix` / `hasSuffix`; just deprioritise.**
Rejected because they add code budget without enabling demos. Cutting
them now avoids implementation effort and lets Phase 7 land sooner
with `struct` + `switch` + `random(in:)` - the features that *do*
enable demos.

**Add `continue` alongside `break`.** Considered; deferred. Demo loops
that would benefit are equally well written with an `if`-guard. Adding
both doubles the loop-compiler complexity for marginal benefit.

**Add `print` with multiple positional arguments (Applesoft `PRINT A;
B; C`).** Considered; rejected as un-Swiftlike. The Swift idiom for
this is multiple `print(_:terminator:)` calls or a single
interpolated string. Keeping the surface area Swifty matters more than
matching Applesoft's exact ergonomics.

## Cost

**Memory cost (BSS, target build):**

| Field                  | Old size       | New size       | Delta     |
|------------------------|----------------|----------------|-----------|
| Source buffer          | 512 B          | 2,048 B        | +1,536 B  |
| Bytecode buffer        | 256 B          | 1,024 B        |   +768 B  |
| Global name table      | 16 × 8 = 128 B | 32 × 12 = 384 B|   +256 B  |
| Function name table    | 8 × 8 = 64 B   | 16 × 12 = 192 B|   +128 B  |
| Local name table       | 8 × 8 = 64 B   | 16 × 12 = 192 B|   +128 B  |
| **Total BSS delta**    |                |                | **~2.8 KB**|

Current SYS-build BSS data ends at `$A12D`. After the raises:
~`$AC2D`, still ~2.7 KB below the cc65 C-stack top at `$B700`
(`$BF00 − $0800 __STACKSIZE__`).

**Memory cost (CODE):** `break` adds ~50 bytes for the per-loop
break-site tracker and the parser case. `print(_:terminator:)` adds
~80 bytes for the second compiler entry point and the string-literal
emit path. Total: ~130 bytes - well under the 127-byte Phase 3
headroom after Phase 4's existing additions are accounted for, but
Phase 4 will exceed the 28 KB budget regardless; this isn't the
deciding factor.

**Performance cost:** Negligible. `break` is one unconditional jump
(no slower than a flag check). `print(_:terminator:)` has no per-call
overhead vs. the default form.

**Code complexity cost:** Low. Both additions are scoped grammar
extensions with no value-representation or VM-architecture impact.
The limit raises are pure constant changes plus regenerating the
name-table sizes in `globals.c` / `compiler.c`.

**Schedule cost:** ~1 day for `break`, ~half day for
`print(_:terminator:)`, ~half day for the limit raises and updated
tests. Total: ~2 days added to Phase 4.

## Migration

No existing tests or programs break. `break` is a new reserved word;
any user identifier called `break` would have been a future hazard
since it's already a Swift keyword. The limit raises are transparently
larger budgets - existing test fixtures still pass without
modification.

`assert` was never implemented. `hasPrefix` / `hasSuffix`, dropped here,
were later reintroduced as Family B builtins in Phase 16 once the Family
B dialect had room for them.

## Decision

Implemented during Phase 4 alongside functions, optionals, and arrays.
`continue` was deliberately deferred (see Alternatives).
