# SWB.md - the `.swb` bytecode file format

`.swb` is SwiftII's **compiled program** format: a serialized, re-runnable
bytecode image. The Family B **Compiler** writes one from a `.swift` source;
the **Runner** loads and executes it without recompiling. It is the on-disk
hand-off between the two - think UCSD Pascal's `.CODE`.

This page is the human-readable reference. The authoritative spec is the
header comment and prototypes in
[`src/swb/swb.h`](../../src/swb/swb.h); the rationale is in design doc
[015](design/015-bigger-programs-pascal-toolchain.md).

---

## Why a file (and not just RAM)?

A Family A interpreter maps its own code over the language card, on top of
ProDOS's MLI body - so it can't keep MLI live and can't chain to another
program. The Family B **Compiler** and **Runner** are separate **MAIN-only**
binaries with MLI intact; they hand off through a disk file instead of RAM:

```
 hello.swift ──[ Compiler ]──▶ hello.swb ──[ Runner ]──▶ output
               compile once                 run, no compile
```

Compiling once and running many times is also what lets a program exceed a
REPL's RAM ceiling - the Compiler streams source through a small window
(design [016](design/016-familyb-bigger-source.md)) and the Runner only ever
holds the finished bytecode.

---

## Image layout

All multi-byte fields are **little-endian**. The header is 12 bytes
(`SWB_HEADER_SIZE`), followed by three variable-length sections in order:

```
 off  size            field
 ───────────────────────────────────────────────────────────────────
  0    3              magic  'S' 'W' 'B'
  3    1              version             (SWB_VERSION, currently 1)
  4    2              program_start       VM entry PC (skips the func arena)
  6    2              bc_len              total bytecode length
  8    2              heap_len            constant-pool size in bytes
 10    1              funcs_count         number of functions (0..MAX_FUNCS)
 11    1              reserved            (0)
 ───────────────────────────────────────────────────────────────────
 12        bc_len     bytecode[]          the program's instructions
 …         heap_len   const-heap[]        compile-time string/array constants
 …         funcs_count × 4   funcs[]      one 4-byte record per function
```

Each **funcs[]** record is 4 bytes (`SWB_FUNC_SIZE`):

```
 off  size  field
  0    2    bc_start        bytecode offset of the function body
  2    1    param_count     number of parameters
  3    1    has_return      1 if the function returns a value, else 0
```

### The sections

- **bytecode** - the compiled instruction stream. The opcode set is the same
  one the VM interprets in RAM; see [OPCODES.md](OPCODES.md). `program_start`
  is where execution begins (function bodies are emitted ahead of top-level
  code, so the entry PC is past them).
- **const-heap** - the compile-time constant pool: the bytes behind string and
  array literals. It is reproduced verbatim into the runtime heap starting at
  offset `STRING_POOL_SLOTS` (16). `OP_STR` and friends carry **array-relative
  heap indices**, not RAM addresses, so copying the byte image in makes every
  offset resolve unchanged - no relocation needed.
- **funcs** - the function table. The Runner never compiles, so it rebuilds
  this table from the file; `OP_CALL` resolves a target through
  `funcs_get_start` / `funcs_get_param_count` at run time.

### What does *not* travel

**Globals.** They are created at run time by `OP_DEFINE_GLOBAL`, so the file
carries no global table - only the bytecode that defines and uses them. Local
variables likewise live only on the VM stack during execution.

---

## Versioning

`SWB_VERSION` (1) is bumped whenever the **opcode set**, the **builtin id
assignments**, or the **funcs-record layout** change - anything that would make
an older Runner misread a newer image. The Runner validates the magic *and*
version in the header and rejects a mismatched Compiler/Runner pair up front
("bad .swb image") rather than dying mid-program on an unknown opcode.

A `.swb` is therefore only guaranteed to run on a Runner built from the same
version. Recompile the `.swift` after a version bump.

The version is a **single byte**, so it can hold **256 values (0–255)**.
That is a hard ceiling - there is no extension field - but in practice it is
no concern: the number only advances on a breaking format change, so reaching
255 would take 255 incompatible revisions.

---

## Producing and consuming a `.swb`

The C API lives in [`src/swb/swb.h`](../../src/swb/swb.h), split into a
write-side and a read-side translation unit (the Compiler links only the
writer, the Runner only the reader - ld65 links whole objects, so a single TU
would make each binary carry the other half as dead code):

| Function | Used by | Purpose |
|----------|---------|---------|
| `swb_write` | host / tools | serialize a compiled program into one image buffer |
| `swb_write_stream` | Compiler | stream the image straight to disk via a writer callback (no full-image buffer in its tight RAM window) |
| `swb_read` | host | deserialize: copy bytecode out + rebuild the heap/funcs singletons |
| `swb_open_image` | Runner | validate + rebuild singletons, then run the bytecode **in place** (no copy) |
| `swb_header_info` / `swb_load_tail` | //e aux Runner (`WITH_AUX_BC`) | two-step paged load: bytecode streamed to aux RAM, the heap+funcs tail rebuilt in MAIN |

The `swb_err_t` enum reports the specific failure (bad magic/version,
truncation, a section exceeding the caller's capacity, or an out-of-bounds
entry PC).

---

## Tooling

- **Build one on your Mac** - `make swbc` builds a host cross-compiler that
  emits the exact same format the on-disk Compiler does:
  ```sh
  make swbc
  build/host/swbc hello.swift hello.swb
  ```
- **Inspect one** - `make disasm FILE=path/to/program.swb` decodes the 12-byte
  header and disassembles the bytecode (it recognizes the `SWB` magic and skips
  the header automatically).

---

## See also

- [`src/swb/swb.h`](../../src/swb/swb.h) - the authoritative spec + C API
- [OPCODES.md](OPCODES.md) - the bytecode the image contains
- [design 015](design/015-bigger-programs-pascal-toolchain.md) - the Compiler/Runner toolchain (why `.swb` exists)
- [design 016](design/016-familyb-bigger-source.md) - streaming source so programs can exceed RAM
- [ARCHITECTURE.md](ARCHITECTURE.md) - where the two binary families fit
