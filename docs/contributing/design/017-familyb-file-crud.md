# 017 - Family B file/directory CRUD builtins

## What

Round out the Family B file-I/O surface (doc 015 shipped `readFile` /
`writeFile`) to full file + directory CRUD:

| SwiftII                                            | id  | MLI                     |
|----------------------------------------------------|-----|-------------------------|
| `deleteFile(_ path: String) -> Bool`               | $28 | DESTROY $C1             |
| `deleteDirectory(_ path: String) -> Bool`          | $28 | DESTROY $C1 (alias)     |
| `renameFile(_ old: String, _ new: String) -> Bool` | $29 | RENAME $C2              |
| `fileExists(_ path: String) -> Bool`               | $2A | GET_FILE_INFO $C4       |
| `appendFile(_ path: String, _ s: String) -> Bool`  | $2B | OPEN+GET_EOF+SET_MARK   |
| `createDirectory(_ path: String) -> Bool`          | $2C | CREATE $C0 (stype $0D)  |
| `listDirectory(_ path: String) -> [String]`        | $2D | OPEN+READ of dir blocks |

Same gating as doc 015: recognized only by the standalone Compiler
(`WITH_SWB`), executed inline by the Runner's lite VM dispatch (MAIN-only,
MLI intact). The Family A interpreters never see these ids - their LC holds
the interpreter, not MLI, and they're at the 64K ceiling. Host builds back
everything with stdio / POSIX (`remove` / `rename` / `mkdir` / `dirent`) so
the whole surface is unit-testable.

## Why

User programs can read and write whole files but cannot delete, rename,
probe, append, or enumerate - half a filesystem. The Runner has 12.5 KB and
the Compiler 6.7 KB of image headroom, so the cost is comfortably absorbable
where it matters (Family B only).

## Decisions

- **`listDirectory` returns `[String]`, not `[String]?`.** The one-byte
  ctype encoding deliberately cannot express optional-of-array (doc 009);
  extending it for one builtin is not worth it. A missing/unopenable
  directory returns the empty array; `fileExists` covers the distinction.
  Entry names are uppercase (ProDOS canonical), files and subdirectories
  both included, volume/subdir header entries and deleted entries skipped.
- **`deleteDirectory` is a compile-time alias of `deleteFile`** (same id).
  ProDOS DESTROY already handles both and refuses a non-empty directory
  (returns `false`); host `remove()` matches. Two names cost one table row,
  no extra VM code.
- **No prefix builtins** (`getPrefix`/`setPrefix`) - user-declined; partial
  paths resolve against the boot prefix, absolute paths cover the rest.
- **Directory block buffer reuses `s_read_buf`** (file_io.c's 1 KB
  `readFile` buffer) rather than a new 512 B static - the Runner's BSS was
  tuned to ~100 B slack in doc 016, so no new big statics. Consequence:
  a `readFile` call invalidates an in-progress `listDirectory` walk; the
  VM builtin runs the walk to completion in one dispatch, so this can't be
  observed from Swift code.
- **`renameFile`'s second ProDOS path lives in a C-stack local** (66 B),
  again to keep BSS flat.
- **One iterator at a time** (`userdir_*` is a singleton), matching the
  one-open-file constraint of the single MLI I/O buffer.
- **Compiler recognition becomes a table** (name/id/argc/out-ctype), the
  same shape as the Phase 10 platform-builtin table - 9 file builtins as
  rows instead of 9 `name_eq` branches.
- **`appendFile` creates the file if absent** (OPEN error → CREATE TXT +
  OPEN), then SET_MARK to EOF and writes. Host: `fopen(path, "ab")`.

## Alternatives considered

- `[String]?` return for `listDirectory` - needs a two-byte ctype encoding
  (doc 009 alternatives); rejected on cost.
- A separate `catalog()` print-only builtin - subsumed by `listDirectory`
  plus a `for` loop; rejected as redundant surface.
- Routing the new ids through the XLC dispatcher - wrong residency: these
  are Family B lite-dispatch builtins like readFile/writeFile, never
  SWIFTSAT/SWIFTAUX.

## Cost

Image: COMPILER.SYSTEM 33957→34217 (+260 B), RUNNER.SYSTEM II+ 28149→29747
(+1598 B), //e 26021→27619 (+1598 B) - all far inside the 40704 B disk
budget. The Family A interpreters (SWIFTIIP/SWIFTSAT/SWIFTIIE/SWIFTAUX) are
byte-identical: they don't define `WITH_SWB`.

The binding constraint was **runtime BSS**, not the disk image. In both
Family B cfgs, BSS overlays ONCE and grows up to `$BF00 - __STACKSIZE__`, so
added CODE (which pushes ONCE up) and added static locals (cc65 `-Cl`) both
eat BSS. Two reclaims:

- **Compiler** (+152 B over): the 7 new builtin name strings + table rows are
  irreducible RODATA. Initially reclaimed by trimming the C-stack reserve
  `$0400→$0300`; later Family B recognizers trimmed it further to the current
  `$0200` reserve recorded in `swiftii-compiler.cfg` and `MEMORY_MAP.md`.
  The compiler is `-Cl`, so recursive descent keeps almost nothing on the
  C stack. ~100 B BSS margin in this slice.
- **Runner** (+1.6 KB over): `vm_file_builtin` + the prodos CRUD MLI calls.
  Reclaimed from three places - `USERFILE_READ_CAP` 1024→512 (the readFile
  buffer doubles as the listDirectory block buffer, and a ProDOS dir block is
  exactly 512 B, so this is nearly free), `HEAP_SIZE` 4608→3840 (~1.9× the
  Family A interpreters' 2 KB), and the C-stack reserve `$0400→$0200` (the VM
  is a flat dispatch loop whose value/call frames live in BSS, so its C-stack
  depth is fixed by the interpreter's own helper chain, not by user-program
  recursion). ~150 B BSS margin.

To keep prodos.c from costing the Compiler dead BSS (it links the TU but
never calls the CRUD functions, and `-Cl` would make their locals static),
the five new `pf_*` functions are gated `WITH_FILE_CRUD` - defined for the
Runner + host only.

On-target verification: the `FBTESTS/` suite (`tfileio` + `tfiledir`) runs
on an emulated Family B compiler-runner (data disk in drive 2), exercising
real ProDOS MLI. Both report `fail 0`; the trimmed Compiler/Runner C-stacks
hold under a real compile.
