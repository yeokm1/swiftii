# ARCHITECTURE.md - how SwiftII fits together

The mental model a contributor needs before changing anything: how source
becomes output, where each responsibility lives, and why the same codebase
ships as several different binaries. This is the orientation map; the
authoritative detail lives in the docs it links to.

Read [DEVELOPING.md](DEVELOPING.md) first for the dev loop, then this, then
the per-topic references ([LANGUAGE.md](../using/LANGUAGE.md),
[MEMORY_MAP.md](MEMORY_MAP.md), [OPCODES.md](OPCODES.md),
[CONSTRAINTS.md](CONSTRAINTS.md), and the numbered [design/](design/)
records).

---

## 1. The one constraint that shapes everything

SwiftII runs on a 1 MHz 6502 with **64 KB of address space**, under ProDOS
2.4.3. A ProDOS `SYS` binary gets `$2000–$BEFF` - **40,704 bytes** - and
that ceiling is the gravity well every design decision falls into. When you
see a feature that ships on one binary but not another, or a function body
exiled to a separate memory bank, the reason is almost always this ceiling.

Two consequences run through the whole codebase:

- **The C is portable and host-testable.** Almost all logic (lexer,
  compiler, VM, runtime) is plain C that compiles for both the Mac host
  (clang) and the Apple II (cc65). The host build is where tests run; the
  Apple II is where it ships. Only the `platform/` layer differs.
- **Code size is a first-class budget.** `make size` reports each binary
  against the 40,704-byte ceiling. A change that works but bloats a binary
  past its headroom has failed. See [CONSTRAINTS.md](CONSTRAINTS.md).

---

## 2. The pipeline: source → output

Both execution modes (REPL and file/program) run the **same** lexer,
compiler, VM, and runtime. The path:

```
            (Apple ][+ only)
 keystrokes ──▶ input layer ──▶ canonical .swift bytes
 platform/apple2/input.c        (lowercase ASCII + digraphs)
                                       │
                                       ▼
                  ┌─────────┐    ┌──────────┐    ┌──────────┐    ┌──────────┐
  .swift source ─▶│  lexer  │──▶ │ compiler │──▶ │ bytecode │──▶ │    VM    │──▶ output
                  │ tokens  │    │ (Pratt)  │    │  (.swb)  │    │ dispatch │
                  └─────────┘    └──────────┘    └──────────┘    └─────┬────┘
                  src/lexer/     src/compiler/   src/vm/opcodes  src/vm/vm.c
                                       │                               │
                                       └──────── src/runtime/ ◀────────┘
                                          heap · values · strings ·
                                          arrays · builtins
```

| Stage | Lives in | Responsibility |
|-------|----------|----------------|
| **Input layer** | [`src/platform/apple2/input.c`](../../src/platform/apple2/input.c) | Apple ][+ only: auto-lowercase, `'` case markers, `Ctrl-W`→`_`, C digraphs. Turns an uppercase-only keyboard into canonical bytes. //e and host skip it. See [design/003](design/003-apple2-input-method.md). |
| **Lexer** | [`src/lexer/`](../../src/lexer/) | Canonical lowercase ASCII → token stream. Keyword match is exact (input already canonical). |
| **Compiler** | [`src/compiler/`](../../src/compiler/) | Single-pass **Pratt** parser emitting bytecode directly - no AST. `pratt.c` (expressions), `statements.c`, `strings.c` (interp), `emit.c`, `globals.c`/`locals.c` (symbol tables), `types.c` (static type tracking). |
| **Bytecode** | opcodes in [`src/vm/opcodes.h`](../../src/vm/opcodes.h), format in [`src/swb/`](../../src/swb/) | A compact stack-machine ISA. On Family B it is serialized to a `.swb` file. See [OPCODES.md](OPCODES.md). |
| **VM** | [`src/vm/vm.c`](../../src/vm/vm.c) | Stack-based dispatch loop. `dispatch.s`/`ops/*.c` are placeholders for a future hand-tuned 6502 path. |
| **Runtime** | [`src/runtime/`](../../src/runtime/) | Heap (bump + LIFO reclaim), tagged values, ref-counted strings/arrays, the static string pool, and the builtins. |

The compiler is **single-pass and AST-free** - it emits bytecode as it
parses. That is a deliberate size choice (no tree to allocate or walk), and
it constrains the language: anything needing a second pass or a tree
rewrite is hard to fit and tends to land in the larger Family B dialect or
get deferred.

---

## 3. The value and memory model

- **Tagged values (3 bytes).** `Int`, `Bool`, `nil`, and the optional tag are
  *immediates* stored directly in the value. `String` and `Array` are *heap
  objects*: the value holds a pointer into the heap. See
  [`src/runtime/value.h`](../../src/runtime/value.h).
- **Heap** - a bump allocator with **LIFO reclaim** (the most recently
  allocated block can be freed cheaply; arbitrary free is not supported).
  Reference counting on strings/arrays is implicit in the VM's stack ops.
  Design rationale in [design/002](design/002-heap-and-strings.md);
  [`src/runtime/heap.c`](../../src/runtime/heap.c).
- **String pool** - string literals live in a static, RODATA-resident pool
  ([`src/runtime/string_pool.c`](../../src/runtime/string_pool.c)) so they
  don't consume heap.
- **The numeric caps are a RAM budget, not a language choice** - identifier
  length, global/function/local counts, heap size, stack depth. They live in
  [`src/common/config.h`](../../src/common/config.h) (with `#ifndef` guards so
  Family B can raise them) and are documented in
  [LANGUAGE.md → Implementation limits](../using/LANGUAGE.md#implementation-limits).

Where every byte of zero-page, main RAM, and the language card goes is in
[MEMORY_MAP.md](MEMORY_MAP.md). **Update it when you allocate zero-page.**

---

## 4. Two execution families

This is the highest-leverage thing to understand. SwiftII ships as **two
families** of binary, distinguished by *what occupies the language card*.

### Family A - the interpreters (REPL + file mode)

The interpreter is split between MAIN and the **language card** (`$D000+`):
warm/cold compiler, REPL, and file-runner bodies live in LC, while hot VM
dispatch, lexer, value/heap, builtins, and platform I/O stay in MAIN. The
user's program and data live in MAIN. `src/main/main.c` dispatches to
[`src/repl/`](../../src/repl/) (interactive) or
[`src/file_runner/`](../../src/file_runner/) (run a `.swift` from disk).
Because the language card holds interpreter code and the MLI body is not
resident as a callable service, Family A has no general file I/O.

Family A is itself split by capability (see section 5): **lite** REPLs and
**extras** REPLs.

### Family B - the on-disk Compiler + Runner

Two **MAIN-only** tools that hand off through a `.swb` file on disk
(design [015](design/015-bigger-programs-pascal-toolchain.md)). They ship in
three paged-toolchain tiers:

- **Tier 1 flat** (`COMPILER.SYSTEM` + `RUNNER.SYSTEM` on the baseline compiler
  disk): no Saturn or aux RAM; Compiler bytecode and Runner `.swb` image live in
  MAIN.
- **Tier 2 Saturn** (II+ Saturn compiler disk): the Compiler flushes completed
  function bodies into Saturn banks through
  [`saturn_bc.s`](../../src/platform/apple2/saturn_bc.s), and the Runner reads
  program bytecode back through a small MAIN window. This raises total bytecode
  capacity for function-heavy programs while each one function/top-level body
  still has to fit the scratch window.
- **Tier 3 //e aux** (//e compiler disk): the same paged bytecode model, backed
  by //e auxiliary RAM through
  [`aux_bc.s`](../../src/platform/apple2/aux_bc.s).

- **Compiler** ([`src/main/compiler_main.c`](../../src/main/compiler_main.c)) -
  streams a `.swift` through a 4 KB low-RAM window
  ([`srcwin.c`](../../src/compiler/srcwin.c), design
  [016](design/016-familyb-bigger-source.md)) so source size is
  **disk-bounded**, not RAM-bounded; writes a `.swb`; chains the Runner.
- **Runner** ([`src/main/runner_main.c`](../../src/main/runner_main.c)) - reads
  the `.swb` and runs it on the VM with all builtins inline. No compiler
  resident.

Because each is MAIN-only with an empty language card, **ProDOS MLI
survives** - which is why file/directory I/O (`readFile`, `listDirectory`, …)
exists *only* in Family B. Family B is also the largest dialect: it raises
the table caps and carries the stretch builtins (`abs`, `sgn`, `random`,
`wait`, `tone`, `for item in array`, `switch`).

> The split is the answer to "why is this an interpreter *and* a compiler?"
> A program too big for a Family A REPL's RAM ceiling is compiled once by the
> Family B Compiler and executed by the Runner, streaming from disk.

The `.swb` write/read code is split into two translation units
([`src/swb/`](../../src/swb/)) because ld65 links whole objects - one TU would
make each binary carry the other half as dead weight. The on-disk format is
documented in [SWB.md](SWB.md).

---

## 5. The binary/build matrix

One source tree, many binaries, selected by **`WITH_*` compile flags** in the
[`Makefile`](../../Makefile) and the per-binary `ld65` configs in
[`src/platform/apple2/`](../../src/platform/apple2/). The audience-facing list
of what each carries is in [API.md](../using/API.md#how-to-read-this-document)
and the cost table in [FEATURES.md](../using/FEATURES.md).

| Binary | Machine | Family | What's special |
|--------|---------|--------|----------------|
| `SWIFTIIP` | ][ / ][+ | A, lite | core language; full MAIN, no spare bank |
| `SWIFTIIE` | //e | A, lite | core + `text`/`text80` (80-col firmware); `WITH_IIE` |
| `SWIFTSAT` | ][+ Saturn 128K | A, extras | extras builtins; cold bodies in **Saturn bank 1** (XLC) |
| `SWIFTAUX` | //e + 64K aux | A, extras | extras builtins; cold bodies **copied down into aux** |
| `COMPILER` | ][+///e | B, Tier 1 | `.swift` → `.swb`; flat MAIN bytecode arena |
| `RUNNER` | ][+///e | B, Tier 1 | executes `.swb` from MAIN; has ProDOS MLI → file I/O |
| `COMPILER` | ][+ Saturn 128K | B, Tier 2 | flushes function bytecode into Saturn banks |
| `RUNNER` | ][+ Saturn 128K | B, Tier 2 | streams `.swb` bytecode through a Saturn-backed window |
| `COMPILER` | //e + 64K aux | B, Tier 3 | flushes function bytecode into aux RAM |
| `RUNNER` | //e + 64K aux | B, Tier 3 | streams `.swb` bytecode through an aux-backed window |
| host | Mac | - | clang build; stubs Apple II hardware for tests |

### How a feature gets out of the way: XLC and aux copy-down

The lite REPLs are full. The **extras** binaries fit more by moving *cold*
function bodies out of MAIN:

- **SWIFTSAT** parks them in **Saturn bank 1** and reaches them through a
  dispatch trampoline ([`xlc.s`](../../src/platform/apple2/xlc.s),
  [`xlc_table.s`](../../src/platform/apple2/xlc_table.s)). This is the "XLC"
  mechanism - design [011](design/011-extras-lc-in-saturn-aux.md).
- **SWIFTAUX** copies grouped overlays **down into //e aux RAM** through a
  packed directory ([`aux_xlc.s`](../../src/platform/apple2/aux_xlc.s),
  packed by [`pack_swiftaux.py`](../../tools/host/diskimg/pack_swiftaux.py)).

A given builtin is therefore *available* or *not* per binary, and *resident
in MAIN* or *in the cold bank* - both are compile-time facts. Calling a
builtin a binary doesn't carry is a **compile error**, not a runtime no-op.

### The host backstop

Every Apple II hardware op has a host stub (`peek`→0, `poke`/`gr`/`tone`→no-op,
file I/O→stdio). That is what lets the **entire** language run under clang in
CI: a graphics or file program compiles and executes on the host (drawing and
timing are verified later on the emulator). The interface is
[`src/platform/platform.h`](../../src/platform/platform.h); backends are
[`src/platform/host/`](../../src/platform/host/) and
[`src/platform/apple2/`](../../src/platform/apple2/).

---

## 6. Boot, launcher, and chaining

A SwiftII disk boots into a **launcher** (`SWIFTII.SYSTEM`,
[`tools/apple2/boot_launcher/`](../../tools/apple2/boot_launcher/)) with a menu:
REPL, a file browser that runs `.swift` programs, and an in-process **editor**
(merged into the launcher - there is no standalone editor binary;
sources in [`src/editor/`](../../src/editor/)). The launcher chains to an
interpreter (or Compiler→Runner) by reading a `SYS` file over `$2000` and
jumping via a zero-page bouncer
([`chain.s`](../../src/platform/apple2/chain.s)); on finish it cold-reboots
back to the launcher. The disk set is one interpreter per disk to live within
the per-disk free-space budget - see [BUILDING.md](BUILDING.md) and the disk
table in the [top-level README](../../README.md).

---

## 7. Where to go next

| You want to… | Read |
|--------------|------|
| Set up the toolchain and run the dev loop | [DEVELOPING.md](DEVELOPING.md) · [BUILDING.md](BUILDING.md) |
| Know the hard limits before you cut code | [CONSTRAINTS.md](CONSTRAINTS.md) |
| Find a file / understand the tree | [PROJECT_LAYOUT.md](PROJECT_LAYOUT.md) |
| Add or change an opcode | [OPCODES.md](OPCODES.md) · `scripts/new_opcode.sh` |
| Add a builtin / language feature | [LANGUAGE.md](../using/LANGUAGE.md) · [`src/compiler/builtin_calls.c`](../../src/compiler/builtin_calls.c) |
| Touch RAM / zero-page | [MEMORY_MAP.md](MEMORY_MAP.md) |
| Understand a past decision | the numbered [design/](design/) records |
| Avoid a known trap | [LESSONS.md](LESSONS.md) |
| Run / add tests | [testing/TESTING.md](../testing/TESTING.md) |

**The golden rule**, restated: every change is measured against the 40,704-byte
ceiling. Write the smallest thing that works, run `make size`, and if it
doesn't fit, the design - not the budget - is what gives.
