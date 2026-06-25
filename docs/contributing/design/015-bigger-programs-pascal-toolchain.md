# 015 - Bigger programs: the Family B Compiler + Runner toolchain

Family B is SwiftII's on-disk toolchain for **bigger programs**: a
standalone **Compiler** turns a `.swift` source file into a `.swb`
bytecode image, and a standalone **Runner** loads and executes that
image. It is a UCSD-Pascal-style `Editor → Compiler → Runner` pipeline
that coordinates through **disk files**, not RAM.

This doc covers why the split exists and how the two tools hand off. The
source-streaming front end is design
[016](016-familyb-bigger-source.md), the file/directory builtins are
[017](017-familyb-file-crud.md), and the `.swb` format reference is
[SWB.md](../SWB.md).

## Why a separate Compiler and Runner

File execution on a **Family A** REPL disk routes a *staged source*
through one combined interpreter: the launcher reads the chosen `.swift`
into `$0C00` (`STAGED_SRC_ADDR`), records the length at `$1B06`, chains
the interpreter, and `repl_run` compiles-and-runs it
([repl.c:126-128](../../../src/repl/repl.c#L126-L128)). That caps a
program at the ~2 KB staged region (`FILE_SRC_SIZE`) plus the
interpreter's 1 KB bytecode / 2 KB heap buffers - fine for the samples
(biggest is `xsnake.swift`, 1,713 B) but too small for anything real,
and the lite binaries sit at the MAIN ceiling (SWIFTIIP ~39 B headroom;
SWIFTSAT ~1 B) with no room to grow buffers.

Splitting execution into a separate compiler and runner binary - each
shedding the other's code for bigger buffers - runs into a hard
constraint:

> An interpreter binary maps its own code into the language card
> (`__LCADDR__ = $D000`), which overwrites ProDOS's MLI body. Once an
> interpreter is running it has **no MLI** and cannot load or chain
> another binary. This is why `:quit` cold-reboots rather than
> re-chaining ([screen.c](../../../src/platform/apple2/screen.c)
> `platform_shutdown`; see LESSONS.md and [014](014-run-from-disk.md)).
> Only the launcher (empty LC, MLI intact) can chain binaries.

So an in-memory "compiler hands a bytecode blob to the runner" chain
can't be driven by the interpreters themselves. Family B's answer: build
the Compiler and Runner **MAIN-only** (no LC use), exactly the
launcher's own trick, so ProDOS's MLI stays intact and each tool can
read/write files and chain the next.

## How the toolchain works

```
Launcher → Editor (save SOURCE.swift)
        → Compiler (read SOURCE.swift, write SOURCE.swb)
        → Runner   (read SOURCE.swb, execute)
        → back to the launcher
```

Because the compiler reads source from disk rather than a fixed RAM
region, program size is **disk-bounded, not RAM-bounded** - no 2 KB cap.
(The streaming source window that delivers this is design
[016](016-familyb-bigger-source.md); the practical program cap is the
bytecode arena.) The runner carries all builtins inline - it sheds the
compiler - so graphics/sound programs run on **any** machine with no
Saturn/aux bank needed. A `.swb` is a real, re-runnable artifact (Pascal
`.CODE`): compile once, run many times.

The editor, Compiler, and Runner share a raw-ProDOS-MLI file layer
([prodos.c](../../../src/runtime/prodos.c) +
[mli.s](../../../src/platform/apple2/mli.s), no malloc), since cc65's
POSIX I/O is too heavy for the budget.

Run-handoff between tools is MAIN-only chaining (`chain.s chain_exec`: a
ZP bouncer that MLI-READs the next SYS file over `$2000` and JMPs), never
an in-RAM blob a chain READ would clobber. The Compiler and Runner
**chain back to the launcher** when done (no cold reboot); the launcher's
LASTRUN note (extended with an origin byte) reopens the editor (if the
run came from the editor's Ctrl-R) or the file selector on the file. A
"Press any key to continue…" prompt precedes the return so output and
errors stay readable.

Launching a Family B program is therefore edit → compile → run with a
disk read at each hop - slower than the REPL's in-memory staged path,
but only at launch.

## The `.swb` handoff format

The Compiler writes the `.swb` **next to its source** with the same base
name (`GREET.SWIFT` → `GREET.SWB`), so a compiled program lives beside
its source and the file selector lists and re-runs it. In the file
selector, `X` on a `.swb` runs the Runner directly (no recompile), `X` on
a `.swift` compiles + runs, and `Enter` opens the file. The Compiler
derives the path (`compiler_main.c derive_swb_path`) and stages it
length-prefixed at `EDIT_PATH_ADDR` for the Runner (`runner_main.c
swb_path`); the launcher stages the same address when a `.swb` is run
directly.

A `.swb` holds bytecode plus the constant heap (string pool). Heap
offsets in `OP_STR` are **array-relative indices into `s_heap[]`** (from
`STRING_POOL_SLOTS` = 16), not RAM addresses
([heap.c:52-65](../../../src/runtime/heap.c#L52-L65)), so the runner
reproduces the byte layout inside its own `s_heap[]`. Layout:

`[ bytecode 0..bc_len ][ heap-const 16..s_heap_ptr ][ funcs 0..n ] + { program_start, bc_len, s_heap_ptr, funcs_count }`

Globals do **not** travel (filled at runtime via `OP_DEFINE_GLOBAL`). The
funcs table's runtime fields **do** travel: `OP_CALL` resolves
`{start_pc, param_count, has_return}` per function via `funcs_get_start`
at runtime ([vm.c:838](../../../src/vm/vm.c#L838)), so the runner loads a
small funcs section (≤ `MAX_FUNCS` = 16 entries) via `funcs_set_start` /
`funcs_set_signature` before `vm_run`. The compile-only fields (function
names, per-param/return ctypes) stay behind. The authoritative spec is
[swb.h](../../../src/swb/swb.h); see also [SWB.md](../SWB.md).

The Compiler also adds Family-B-only Swift file I/O builtins (`readFile`
/ `writeFile`); the full file/directory CRUD surface is design
[017](017-familyb-file-crud.md).

## Disks and tiers

Family B ships as **compiler disks** alongside the Family A REPL disks -
additive, with the existing REPL disk set unchanged. Each Family B disk
carries the launcher (with the in-process editor) plus the on-disk
**Compiler** and **Runner**, and **no REPL** - dropping the REPL is what
frees the room for editor + compiler + runner. The demo `SAMPLES/` ship
on the Family B disks (they did not need to move to a separate data
disk).

Both tools are per-machine. The Runner needs `WITH_IIE` for its keyboard
**and** display model; the Compiler has no typing model but still renders
its `Compiling:` / `Wrote:` path echo, so it too is `WITH_IIE` on the //e
(full-ASCII render) and pre-IIe on the II+/Saturn (inverse-letter render
with the `WITH_INVERSE_JM` `J`/`M` fix - see
[003](003-apple2-input-method.md)). There are three Compiler tiers, one
per disk, differing in how much **source** they can handle: II+ Tier-1
(flat), II+ Tier-2 (Saturn-paged), and //e Tier-3 (aux-paged). See
[BUILDING.md](../BUILDING.md) for the disk/target matrix and
[016](016-familyb-bigger-source.md) for the paging.

Each tool must fit MAIN (`$2000-$BEFF`, ~40 KB) **without** the LC, so
the MLI body in LC bank 1 survives:

- **Editor** - MAIN-only, merged in-process into the launcher
  ([006](006-editor.md)); not a standalone binary.
- **Runner** - VM + runtime + all builtins, no compiler (~23 KB). The
  VM/runtime never needed the LC; it was the *compiler's* cold code
  (`pratt`, `statements`, `types`) that lived there.
- **Compiler** - lexer + parser + emit (~32 KB). The cold code returns to
  MAIN, but because it streams source from disk it needs almost no RAM
  source buffer.

## Alternatives considered

- **In-memory compiler→runner chain** (the original
  [014](014-run-from-disk.md) idea). Rejected: interpreters can't chain
  (the MLI/LC constraint), and a blob staged in RAM must survive the
  runner's `$2000` chain READ. Routing through disk files removes both
  problems - what Pascal did.
- **Bank-staged big source on Saturn/aux** (load source into the bank,
  streaming lexer). Works within one binary but is extras-only (bare lite
  has no bank) and needs a streaming-from-bank lexer with Saturn
  LC-arbitration. Disk streaming is simpler, all-machines, and unbounded.
- **Single combined binary, raise the cap.** Impossible - lite is at the
  MAIN ceiling (~39 B), no room to grow buffers.
- **Hand-assembly to reclaim RAM.** Frees space but is months of work and
  kills the host C test suite. Keep "C bulk + targeted asm."
- **Merge II+/IIe lite into one adaptive binary.** Budget-blocked and
  saves no testing.

The host `tests/` harness exercises the compiler → `.swb` → runner path
on x86 and diffs output against the combined interpreter, so the
language/VM stay covered once.
