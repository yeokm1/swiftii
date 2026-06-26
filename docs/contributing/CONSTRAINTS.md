# CONSTRAINTS.md

This file describes the **hard, non-negotiable constraints** of the SwiftII
target platform and toolchain. Any code generated for this project must
respect every constraint listed here. When in doubt, ask before assuming a
modern convenience is available.

---

## Target hardware

- **Machine**: the Apple II Plus is the baseline target (original,
  non-enhanced); the original Apple ][ (booted via ProDOS 2.4.3) and the
  Apple //e are also supported. The boot disk declares the machine - there is
  no runtime hardware probe to pick a binary; each disk ships the binaries for
  its target. Supported configurations:
  - **II+ (or original ][) + 16 KB language card - 64 KB.** The baseline:
    lite REPL (`SWIFTIIP`) + the Family B Tier-1 toolchain. The LC holds cold
    interpreter code / ProDOS's MLI body; pre-IIe keyboard + display
    restrictions apply (see below).
  - **II+ + Saturn 128 KB card.** Adds the Saturn extras REPL (`SWIFTSAT`;
    cold VM bodies live in Saturn bank 1) and the Family B **Tier-2** toolchain
    (bytecode paged into Saturn banks, `saturn_bc.s`).
  - **II+ + Videx Videoterm (slot 3) - optional 80-column.** The REPL Videx
    path is `SWIFTSAT`, so it also requires the Saturn 128 KB card (opt-in
    `text80()`/`text()`/`htab`). The Family B `RUNNER` can also drive a
    Videoterm for program output on the flat II+ tier. The lite REPL /
    launcher / editor stay 40-column (RAM-walled). See
    docs/contributing/design/013-80col-text.md (track B).
  - **//e, with or without an Apple 80-Column Text Card - 64 KB.** The //e
    lite REPL (`SWIFTIIE`; native lowercase + case input, no input/display
    substitution) and a **//e-native Tier-1** Family B toolchain — the flat
    Compiler/Runner built `WITH_IIE` (native case rendering) with the Runner
    carrying the //e **firmware** 80-col arm, so a program's `text80()` works on
    a //e with the basic 1 KB 80-Column Text Card. This is a distinct build from
    the II+ Tier-1 toolchain (which has no //e firmware 80-col and uses the
    inverse-video case hack); a non-extended //e gets the //e flavor, not the
    II+ binary. Flat 1,834 B program cap (no aux paging).
  - **//e + 64 KB extended 80-column card (aux RAM) - 128 KB.** Adds the aux
    extras REPL (`SWIFTAUX`; cold bodies copied down from aux) and the Family B
    **Tier-3** toolchain (bytecode paged into aux, `aux_bc.s`), plus 80-col
    text. See "Family B program-size limits" for what each unlocks per program.
- **CPU**: MOS 6502 @ 1.023 MHz
- **RAM**: **64 KB directly addressable** (48 KB main RAM + 16 KB language
  card) - the hard budget all binaries fit. RAM beyond 64 KB (a Saturn 128 KB
  card, or the //e auxiliary 64 KB) is **never directly addressable**; it is
  bank-paged in/out a window at runtime, used only for cold extras code
  (`SWIFTSAT`/`SWIFTAUX`) or paged Family B bytecode (Tiers 2/3).
- **No FPU.** No hardware multiply or divide. No barrel shifter.
- **Registers**: A, X, Y are all 8-bit. No general-purpose 16-bit registers.
- **Hardware stack**: 256 bytes only, fixed at $0100–$01FF. Recursion is
  effectively forbidden beyond shallow depths.
- **Zero page** ($0000–$00FF) is precious - fastest addressing, but shared
  with ProDOS, the cc65 runtime, and any resident interpreter (Applesoft,
  BASIC.SYSTEM). See `MEMORY_MAP.md` for our zero-page assignments.
- **Keyboard character set is restricted.** The stock //+ keyboard
  produces uppercase letters, digits, and the punctuation
  `! " # $ % & ' ( ) * + , - . / : ; < = > ? @ ^` plus space - and
  nothing else. Specifically, `{ } [ ] \ _ ` `` ` `` `| ~` and lowercase
  letters are not on the keyboard. Canonical `.SWIFT` files are
  always lowercase ASCII regardless of authoring machine; on //+
  this invariant is enforced by an **input layer**
  (`src/platform/apple2/input.c`) that runs above the keyboard and
  below storage. Auto-lowercases letters by default; `'` is a
  single-letter case marker; `''` is a run case marker; `Ctrl-W`
  produces `_`; C-standard digraphs `<%` `%>` `<:` `:>` `??/`
  produce `{ } [ ] \` (and `??!` is reserved for `|`). See
  `docs/contributing/design/003-apple2-input-method.md` revision 3 for the full
  scheme. The lexer requires exact lowercase keywords (input arrives
  canonical); the host build skips the input layer entirely since
  host keyboards already produce canonical bytes.
- **Display character set is also restricted on pre-IIe machines.**
  The original Apple II and the II+ share a character ROM with
  glyphs only for `$20`–`$5F`, so lowercase letters and `{ } |` have
  no native rendering. The screen-write path
  (`src/platform/apple2/screen.c`) substitutes on pre-IIe: lowercase
  letters render as **normal** uppercase and capital letters as
  **inverse** uppercase (so the user can distinguish canonical case at a
  glance - design doc 003 rev 4/5); `{` → `<%`, `}` → `%>`, `|` → `??!`. The "pre-IIe" cohort is the
  original ][ (`$FBB3 == $38`) and the II+ (`$EA`) per Apple Technical
  Note #7 — two *different* bytes, both glyph-poor; the IIe family is
  `$06` (sub-distinguished by `$FBC0`/`$FBDD`). The interpreter does
  **not** read `$FBB3` at runtime — it runs with the language card banked
  in, so that load returns RAM, not the ROM ID byte. The pre-IIe-vs-IIe
  render choice is made by the `WITH_IIE` build flag (the disk picks it);
  `machine_type` in `src/platform/apple2/osdetect.c` keeps a conservative
  pre-IIe default and `screen.c` substitutes via the
  `APPLE_II_IS_PRE_IIE()` cohort test (`!= $06`, so it fails safe on the
  II+'s `$EA` and on garbage). The stored bytes are unchanged; only the
  rendering on pre-IIe differs. IIe and later machines render every
  canonical byte natively.

## Target operating system

- **ProDOS 8 - reference version ProDOS 2.4.3** (the John Brooks
  revival, which still runs on a stock Apple II Plus). All development,
  testing, and the canonical disk image use 2.4.3. There is no
  secondary OS target.

Code outside `src/platform/apple2/` should not assume direct ProDOS
calls; OS-specific calls live behind the interfaces in
`src/platform/apple2/`.

## Toolchain

- **Compiler suite**: cc65 - driver `cl65`, compiler `cc65`,
  assembler `ca65`, linker `ld65`.
- **Language**: C89/C99 as supported by cc65 (cc65 does not implement
  full C99; some features like designated initializers and VLAs are
  missing).
- **Build target**: `apple2` (the original non-enhanced target). Do
  **not** use `apple2enh` - that target requires a 65C02 and IIe
  enhanced ROM that the II Plus does not have.
- **Linker config**: `.cfg` files for `ld65`, located at
  `src/platform/apple2/`.
- **Build system**: GNU Make.
- **Host platform for development**: macOS (Apple Silicon and Intel).

The same C source compiles for the host (under clang) and for the
Apple II (under cc65) wherever it doesn't touch platform-specific I/O.
This is essential to our test-on-host workflow - see `testing/TESTING.md`.

## Memory budget

This section is the **Family A interpreter** budget - the REPL/lite binaries
(`SWIFTIIP`/`SWIFTIIE`) and their extras siblings (`SWIFTSAT`/`SWIFTAUX`), which
compile *and* run in one resident image. The standalone **Family B**
Compiler/Runner are budgeted separately, and their program-size caps live in
"Family B program-size limits" above (the bytecode row below is the Family A
REPL's in-RAM scratch, **not** the Family B compiler arena).

The interpreter, bytecode, heap, REPL buffers, and call stack must
fit inside the ProDOS SYS file-load ceiling at **$BF00** ($2000–$BEFF
is ours, minus the cc65 C-stack at the top). A SYS binary cannot
exceed **40,704 bytes** on disk; the LC segment is copied off the
binary into the language card at startup but still counts against the
file size. "Warm but not hot" code (`pratt.c`, `statements.c`,
`repl.c`, `metacmds.c`, `file_runner.c`) lives in the language card;
hot paths (VM dispatch, lexer, value/heap ops, builtins, platform I/O)
stay in main-RAM CODE.

| Region                | Today              | Target          | Notes                                    |
|-----------------------|--------------------|-----------------|------------------------------------------|
| Interpreter code      | ~28 KB CODE + ~10 KB LC | ≤ 28 KB main + LC for cold | binary ~40,691 B total (lite); see below |
| Compiled bytecode     | 1 KB shared buffer | ≤ 8 KB          | Family A REPL scratch: function arena + top-level scratch (`bcbuf`). Family B caps are tiered - see above. |
| Heap (objects)        | 2 KB               | ≤ 6 KB          | strings, arrays                          |
| VM stack              | 32 slots (96 B)    | ~2000 slots (~6 KB) | software stack, separate from 6502 stack |
| REPL/source buffers   | 2 KB               | ≤ 2 KB          | source buffer (`FILE_SRC_SIZE`)          |
| Globals + symbols     | ≤ 1 KB             | ≤ 2 KB          | 32 globals + 16 funcs + 11-char names    |

The lite binary (`SWIFTIIP.SYSTEM` for II+/earlier, `SWIFTIIE.SYSTEM`
for //e and later - same sources, the //e build adds native case +
lowercase via `-DWITH_IIE`) sits within a few hundred bytes of the
40,704 B ProDOS SYS ceiling on the II+ side (run `make size` for the
authoritative current figures - don't trust numbers quoted in prose).
The extras binaries
(`SWIFTSAT.SYSTEM` for Saturn, `SWIFTAUX.SYSTEM` for //e aux) add the
extras builtin surface in an XLC region outside the main image, so each
keeps its own MAIN under the same ceiling. The binding constraint is the
SYS-file ceiling, not the
28 KB language-card target - code that pushes the file past 40,704 B
fails to load on real ProDOS. The 28 KB figure is the eventual
*main-RAM CODE* shrink target so the interpreter could one day fit
entirely in LC; today main-RAM CODE is ~28 KB plus ~10 KB in LC, and
the migration is partial.

`make size` reports current segment sizes and prints the per-segment
linker map. If your change pushes the binary past the 40,704 B
ceiling, the fix is almost never to raise the budget - see design
doc 008 for the standing reclaim list.

The language card's $D000-$DFFF region is **double-banked** (two 4 KB
banks selectable via soft switches), giving 16 KB total in the
language card region but only 12 KB visible at once. The
plan uses bank 1 for cold code (REPL meta-commands, error reporting,
disassembler), bank 2 reserved for Tier 2 hardware-capability features
(the doc-010 language tiers - unrelated to the Family B paged-toolchain
tiers below), and the common
$E000–$FFFF region (8 KB) for hot paths. Only the single-bank
migration is wired up today - see `MEMORY_MAP.md`.

---

## Dual execution modes - REPL and file

SwiftII must support both:

1. **REPL mode**: the default when a Family A interpreter starts.
   Reads input lines from the user, parses, compiles, and executes
   incrementally. Globals and function definitions persist across
   lines. Errors do not crash the session.
2. **File mode**: there is no OS command line under ProDOS SYS, so a
   file run is launched from the **boot launcher**, two
   ways. *Family A*: the launcher stages the whole source into low
   RAM (`$0C00`, `FILE_SRC_SIZE` ≈ 2 KB) and chains the interpreter,
   which compiles and runs it in place. *Family B*: the
   launcher chains the standalone **Compiler**, which streams source
   from disk through a sliding window (disk-bounded size), writes a
   `.swb`, and chains the **Runner**.

The lexer, compiler, and VM are **shared** between the modes. The
only difference is the driver: REPL uses an interactive read-compile-
eval loop with a per-line scratch bytecode area; a file run does one
compile-then-execute pass.

Constraints implied by REPL support:

- The compiler must be **incremental**: it can compile a single
  statement at a time, reusing the existing global symbol table and
  function table.
- Top-level statement bytecode lives in a **scratch area** that is
  reset before each new REPL input.
- Function bytecode lives in a **persistent area** (append-only), so
  functions defined in earlier lines remain callable.
- Error recovery in the parser must be clean: a parse error in one
  REPL line must not corrupt the symbol table or partially-emitted
  bytecode in a way that affects subsequent lines.

See `MEMORY_MAP.md` for the physical layout of these areas.

---

## Family B program-size limits (the paged toolchain)

The Family B Compiler/Runner ship in **three tiers** (these are
*paged-toolchain* tiers - distinct from the doc-010 hardware-capability
"Tier 1/Tier 2" language levels referenced under Memory budget), selected by
the boot disk (the disk declares the machine - there is no runtime probe). Tiers 2 and 3
page bytecode out of MAIN into spare RAM (Saturn banks / aux), lifting the
program-size ceiling for *function-heavy* programs. The hard caps below come
from the per-tier build flags (`Makefile`: `COMPILER_DEFS` / `RUNNER_IIP_DEFS`
for II+ Tier 1, `COMPILER_IIE_DEFS` / `RUNNER_IIE_DEFS` for the //e-native flat
Tier 1, `COMPILER_IIEAUX_DEFS` / `AUX_BC_DEFS`, `COMPILER_SAT_DEFS` /
`RUNNER_SAT_DEFS`) and the paging code (`src/vm/bcwin.c`, `src/compiler/bcbuf.c`, the store drivers
`src/platform/apple2/aux_bc.s` / `saturn_bc.s`). See the 3-tier plan and design
docs 015/016 for the rationale.

| Limit | **Tier 1 - baseline** | **Tier 2 - Saturn** | **Tier 3 - aux** |
|---|---|---|---|
| Machine | II+ 64 K, **or** any //e (two flat builds — see below) | II+ + Saturn 128 K | //e + extended 80-col (aux 64 K) |
| Bytecode store | none (flat MAIN buffer) | Saturn banks 1–7, `$D000` window | aux park `$2000–$BFFF` |
| Max source **file** | disk-bound | disk-bound | disk-bound |
| Max single **statement** | 4 KB | 4 KB | 4 KB |
| Max **total bytecode** | **1,834 B** | **~64 KB** | **~40 KB** |
| …max one function | (shares the 1,834 B) | 640 B | 896 B |
| …max top-level (`main`) | (shares the 1,834 B) | 640 B | 896 B |
| Max const pool (literals) | 768 B | 704 B | 744 B |
| Max funcs / globals | 24 / 48 | 24 / 48 | 24 / 48 |
| Runner whole-`.swb` cap | 2,944 B (loaded into MAIN) | streams (park); tail ≤ 1,024 B | streams (park); tail ≤ 1,024 B |

Key nuances:

- **Source is streamed from disk** (`srcwin.c`), so total source size is bounded
  by disk free space on all tiers; only a single *statement* (including any
  string literal) must fit the 4 KB source window (`FILE_SRC_SIZE`).
- **Tier 1 is flat**: top-level + every function + all literals share the one
  1,834 B arena, and the Runner loads the whole `.swb` into a 2,944 B MAIN
  buffer. Total bytecode ≈ 1.8 KB is the hard ceiling.
- **Tier 1 ships in two flavors**, same flat caps, different render path:
  the **II+** build (`iip-compiler` disk; pre-IIe inverse-video case hack,
  optional Videx 80-col on the Runner) and the **//e-native** build
  (`iie-compiler` disk; `WITH_IIE` native lowercase, Runner has the //e
  **firmware** 80-col arm so `text80()` works on a //e with the 1 KB 80-Column
  Text Card — no extended aux card needed). A //e *without* the 64 KB extended
  aux card uses the //e-native Tier-1 disk, not the II+ one.
- **Tiers 2 & 3 lift the *total* ceiling only for function-heavy programs.**
  Completed function bodies flush to the store (append-only, never re-read), so
  total code can approach the park size. But two things stay MAIN-resident and
  so keep the smaller window cap: **each individual function** (compiled in
  scratch before it flushes) and the **top-level `main`** (never flushed). This
  is why `xfuncs` (many small functions, total bytecode over the flat buffer)
  compiles on Tiers 2/3 but not Tier 1; and, conversely, why a single large
  top-level `main` only fits Tier 1. The lever for the latter is to **wrap
  top-level work in functions** so each body flushes and the resident window
  stays small - `xbig` did exactly this (it was Tier-1-only as one big top-level
  run; wrapping each section makes it compile on all three tiers).
- **Saturn ~64 KB vs aux ~40 KB**: Saturn banks 1–7 hold 84 KB physically, but
  the bytecode offset is a `uint16_t`, so ~64 KB is the practical cap; the aux
  park is physically bounded by `$2000–$BFFF` ≈ 40 KB.
- The Runner's read window (512 B Saturn / 1 KB aux) does **not** cap program
  size - it only changes how often control flow that leaves the window repages.
- The end-to-end maximum for a tier is `min(Compiler-can-emit, Runner-can-run)`;
  both halves are paged on Tiers 2 and 3, so the lift shows on both.

---

## Forbidden constructs

The following must **never** appear in generated code without an
explicit discussion first:

1. **`float` / `double` C types.** cc65 **rejects them outright**:
   compiling code with a `float` or `double` declaration produces
   `Fatal: Floating point type is currently unsupported` and aborts the
   build. There is no cc65 float library - there is no float type at
   all. Any C code that cross-compiles to cc65 must therefore avoid
   the keywords entirely; do not `#include <math.h>` either (cc65 ships
   only an empty stub). SwiftII is integer-only as a result; a `Double`
   type stays parked in `ROADMAP.md section Maybe / probably never` item 2.
2. **`malloc` / `free` / `calloc` / `realloc`.** We have a custom heap
   allocator (`heap_alloc`, `heap_release`, `heap_retain`) tuned for
   our object sizes. cc65's libc malloc works but its sbrk-style heap
   layout fights ours; never mix them.
3. **`printf` / `fprintf` / `sprintf` and friends.** cc65's printf
   pulls in a multi-kilobyte formatting runtime. Use our minimal
   `print_str`, `print_i16`, `print_hex` helpers instead. `puts` and
   `putchar` are acceptable; they're small.
4. **High-level `stdio.h` file I/O.** cc65 provides `fopen`/`fread`/
   `fwrite` over ProDOS, but its POSIX layer costs ~4 KB of code plus
   a `malloc`'d 1 KB ProDOS buffer. **All target file I/O goes through
   the raw MLI wrappers in `src/runtime/prodos.{c,h}`** (`pf_open_read`
   / `pf_read` / `pf_write` / … over a fixed `$1C00` buffer, one open
   file at a time) - the Family B tools, the launcher/editor, and the
   `readFile`/`writeFile`/`appendFile` + `deleteFile`/`renameFile`/
   `fileExists`/`createDirectory`/`deleteDirectory`/`listDirectory`
   builtins (doc 017) all use them; the Family A interpreters link no
   file I/O at all (the launcher stages source for them). On the host,
   `prodos.c` maps the same `pf_*` API onto stdio/POSIX.
5. **Recursion deeper than 4 levels in C code.** The 6502 hardware
   stack is 256 bytes and is also used for cc65's return addresses and
   register saves. The Pratt parser's bounded recursion (≈ 10 precedence
   levels) is acceptable; arbitrary recursion is not. Convert recursive
   algorithms to iterative form using the VM's software stack.
6. **Variable-length stack arrays** (`int buf[n]` where `n` is a
   runtime value). cc65 doesn't fully support VLAs anyway, but don't
   even try. Allocate from the heap or use a fixed-size buffer.
7. **`long` / `long long` arithmetic in hot paths.** cc65's 32-bit
   math is implemented as runtime helper calls and is dramatically
   slower than 16-bit. Use `long` only when truly needed (file
   offsets, large counters); audit hot loops.
8. **Implicit allocation.** Anything in `<string.h>` that returns a
   freshly-allocated buffer (`strdup`) is forbidden. `memcpy`,
   `memset`, `strlen`, `strcmp`, `strcpy` (with caution) are fine.
9. **`#pragma optimize` toggles scattered through code.** Set
   optimization globally in the Makefile (current: `-O -Cl -Or`,
   with register-variable use enabled). Per-file
   tweaks should be exceptional and commented.

## Required practices

1. **Default integer types**: prefer `unsigned char` and
   `unsigned int` (cc65's `int` is 16-bit signed). Use the `<stdint.h>`
   typedefs (`uint8_t`, `uint16_t`, `int16_t`) where it makes intent
   clearer. Never assume `int` is any particular width.
2. **`const` everything that doesn't change.** ld65 places `const`
   data into the `RODATA` segment, which can be linked into the language
   card region rather than precious main RAM.
3. **Use `__fastcall__` for hot single-argument C functions.** The
   default cc65 calling convention pushes args on a software stack;
   `__fastcall__` passes the last (or only) arg in registers. Apply to
   inner-loop helpers like `is_digit(c)`, `value_is_int(v)`, etc.
4. **Use `static` for module-private functions.** ld65's link-time
   inlining is limited, but `static` plus small functions plus `-Cl`
   (static locals) gets you most of the wins.
5. **Tables over computation.** A 256-byte lookup table is almost
   always faster and smaller than the equivalent arithmetic on a 6502.
   For lexer character classes (is_digit, is_ident_start, etc.), a
   single 256-byte table indexed by character beats individual
   comparisons.
6. **Static allocation by default.** If the lifetime of an object
   spans the whole program, declare it as a file-static global.
   cc65's `#pragma static-locals(on)` extends this to function locals
   in non-reentrant functions, saving stack bytes.
7. **Packed enums.** Use `unsigned char` typedefs for enum-like
   values. Default `enum` is `int` (16-bit on cc65), which doubles
   the size of opcode and tag fields.
8. **Separate hot and cold code.** Hot paths (lexer inner loop, VM
   dispatch, value type checks) get hand-tuned attention and
   sometimes assembly. Cold paths (error reporting, disassembly,
   REPL meta-commands, debug output) live in a different code segment
   (linked to the bank-switched $D000 region).
9. **Zero-page variables via `#pragma zpsym`.** The cc65 runtime
   reserves a chunk of zero page for itself; we declare our hot
   variables (VM PC, stack pointer, etc.) in zero page through this
   mechanism. See `src/common/zeropage.h`.

---

## Architecture invariants

These are decisions already made. Don't relitigate them in generated
code. If you want to change one, write a `docs/contributing/design/NNN-*.md`
proposal first.

- **Bytecode VM**, not a tree-walking interpreter. No AST is constructed
  at runtime; the parser emits bytecode directly (single-pass
  compilation, à la *Crafting Interpreters* Part III).
- **Stack-based VM**, not register-based. Simpler dispatch, smaller
  bytecode.
- **Tagged values**, 3 bytes wide: 1 byte tag + 2 bytes payload.
  Payload is either an immediate (small int, bool, nil) or a 16-bit
  pointer into the heap. See `OPCODES.md` for the tag byte layout.
- **Reference counting** for heap objects. No tracing GC. Cycles are a
  known limitation accepted for v1.
- **Source size is RAM-capped in Family A, disk-bounded in Family B.**
  Family A file runs compile a launcher-staged in-RAM copy (≈ 2 KB
  cap); the Family B Compiler streams source from disk through a
  sliding window (`src/compiler/srcwin.c`), refilled at statement
  boundaries - one statement must fit the window, the file need not.
  See "Family B program-size limits" above for the per-tier source,
  bytecode, and `.swb` caps.
- **Opcodes are 1 byte**, with 0–2 bytes of inline operands. ≤ 256
  opcodes total.
- **REPL persists globals and functions across lines**, but discards
  top-level statement bytecode after execution.
- **Single OS target**: one cc65 build, ProDOS 2.4.3 only. OS-specific
  code hides behind `src/platform/apple2/`.

---

## Portability

The lexer, parser/compiler, bytecode definitions, and most of the VM
are written in **portable C** that compiles cleanly with both `clang`
(host) and `cc65` (target). This is deliberate: it lets us run the
entire test suite on the Mac in milliseconds.

Watch for cc65's quirks when writing portable C:

- cc65's `int` is 16-bit; clang's is 32-bit. Use sized typedefs.
- cc65's `char` is unsigned by default; clang's is signed by default.
  Be explicit (`signed char`, `unsigned char`) where it matters.
- cc65 doesn't support some C99 features (designated initializers in
  some contexts, compound literals in some contexts, VLAs).
- cc65's preprocessor is slightly less liberal about `#if` expressions
  than clang's.

When in doubt, build for both targets and let the compilers tell you.

Platform-specific code (ProDOS I/O, screen output, keyboard input,
anything touching $C000-page soft switches) lives behind the
interfaces in `src/platform/`, with a `host/` implementation for Mac
builds and an `apple2/` implementation for the real target.

**Never** introduce target-specific code into core interpreter modules.
If you need an OS service, add a function to the platform interface.

---

## When in doubt

If a piece of generated code is borderline on any of the above -
especially memory size, stack depth, or REPL state safety - flag it
explicitly in a comment and ask before merging. It is much cheaper to
discuss a design tradeoff than to discover in week six that the
allocator fragments because someone slipped a `malloc` into the
parser.
