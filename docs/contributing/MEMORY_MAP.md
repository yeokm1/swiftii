# MEMORY_MAP.md

Where SwiftII puts everything in the Apple II's 64K address space. The
current source of truth is the linker configs under
`src/platform/apple2/` plus `make size`; this file records the current
layout of every shipped binary, zero-page ownership, and the heap /
value representation. Where a region's exact addresses are picked by
ld65 rather than fixed, this file says so and points at the config.

If you allocate a new zero-page slot, a new buffer, or move a section,
**update this file in the same commit**.

The reference operating system is **ProDOS 2.4.3**.

---

## OS context - ProDOS SYS

A ProDOS SYS file loaded without BASIC.SYSTEM owns all of
$2000–$BEFF; ProDOS reserves only $BF00–$BFFF for its global page.
The entire $9600–$BEFF range is ours - it is **not** "ProDOS file
buffers" in this context. That label applies only when BASIC.SYSTEM is
also resident.

Published ProDOS references quote smaller figures that look like they
conflict but do not. The ProDOS 8 technical reference (e.g. prodos8.com)
lists $8F00 (36,608 B) as the largest loadable system program and $B700
(46,848 B) as the total space available; both slice the same map
differently. Our 40,704 B is $9F00 ($2000–$BEFF), the contiguous SYS load
image capped by the global page at $BF00. The 36,608 B figure reserves
$1000 at the top ($AF00–$BEFF) for ProDOS-allocated file buffers, which
only matters when ProDOS owns those buffers (BASIC.SYSTEM resident) -
SwiftII loads as a pure SYS program and manages its own I/O, so the whole
$2000–$BEFF window is ours. The 46,848 B figure is $B700 ($0800–$BEFF):
the 40,704 B load region plus the $0800–$1FFF low RAM a running program
may also use.

Two binary shapes ship from this tree, with different ceilings:

- **Interpreters** (`SWIFTIIP` / `SWIFTIIE` / `SWIFTSAT` / `SWIFTAUX`,
  and the launcher) are single SYS files that use the language card for
  cold code. Their main-RAM image plus BSS must fit under the C-stack
  reservation: BSS ceiling `= $BF00 − __STACKSIZE__` `= $BF00 − $0400`
  `= $BB00`. The whole file must also fit the 40,704 B ProDOS SYS load
  ceiling, enforced by `make size`.
- **Family B tools** (`COMPILER` / `RUNNER`) are **MAIN-only** SYS
  binaries with an empty language card, so ProDOS's MLI body survives
  underneath them (design docs 015/016). They use smaller per-tool
  stack reserves - see the Family B section below.

---

## Shipped binaries

Each binary loads at $2000 and occupies the same main-RAM region; they
never co-reside. One interpreter (or one Compiler/Runner pair) ships
per boot disk.

| Binary | Disk | Notes |
|--------|------|-------|
| `SWIFTII.SYSTEM`  | every boot disk | the **launcher** (`tools/apple2/boot_launcher/boot_launcher.c`, ~34 KB): boot menu + file browser + in-process editor; chains the disk's interpreter / Compiler via an MLI READ + ZP bouncer |
| `SWIFTIIP.SYSTEM` | II+ lite | lite interpreter, //+ typing model |
| `SWIFTIIE.SYSTEM` | //e lite | lite interpreter (`WITH_IIE`: native case + 80-col) |
| `SWIFTSAT.SYSTEM` | II+ Saturn | extras via XLC in Saturn bank 1 |
| `SWIFTAUX.SYSTEM` | //e aux | extras via aux copy-down park |
| `COMPILER.SYSTEM` | Family B | MAIN-only standalone compiler |
| `RUNNER.SYSTEM`   | Family B | MAIN-only `.swb` runner, per-machine builds |

Retired / never built: the unified `SWIFTIIX.SYSTEM`, the standalone
`SWIFTED.SYSTEM` editor (merged into the launcher), the original 462-B
asm boot selector (`boot_launcher.s`,
replaced by the C launcher), and the HGR variants `SWIFTIIH` /
`SWIFTIIF` (HGR moved to ROADMAP Maybe item 1).

The interpreter builds come from one source tree:

- `make apple2`          → `build/apple2/SWIFTIIP.SYSTEM`        (II+ lite)
- `make apple2-iie`      → `build/apple2/iie/SWIFTIIE.SYSTEM`    (//e lite)
- `make apple2-swiftsat` → `build/apple2/swiftsat/SWIFTSAT.SYSTEM` (Saturn)
- `make apple2-swiftaux` → `build/apple2/swiftaux/SWIFTAUX.SYSTEM` (//e aux)

The lite II+/​//e split is by `WITH_IIE`. The two extras builds are
separate: SWIFTSAT uses the `WITH_EXTRAS`/`WITH_SWIFTSAT` path, while
SWIFTAUX uses `WITH_SWIFTAUX` plus `WITH_IIE` and aux/80-column flags.
Per-binary budgets are enforced by `make size` and exercised in `make ci`.

Release snapshot, v1.0.1 rebuilt 2026-06-27 (`make release` + `make size`):

| Binary / region | Bytes | Headroom | Map notes |
|-----------------|------:|---------:|-----------|
| `SWIFTIIP.SYSTEM` | 40,691 | 13 | BSS `$96E2-$AFE5`; LC `$D000-$F70E` |
| `SWIFTIIE.SYSTEM` | 40,183 | 521 | BSS `$9474-$B20A`; LC `$D000-$F780` |
| `SWIFTSAT` MAIN | 40,681 | 23 | BSS `$968C-$B08A`; LC `$D000-$F75A` |
| `SWIFTSAT` XLC | 7,595 | 4,693 | Saturn bank-1 XLC `$D000-$EDAA` |
| `SWIFTAUX` MAIN | 39,926 | 778 | BSS `$933B-$B1CD`; staging starts `$B000` |
| `SWIFTAUX` aux park | 7,522 | - | packed aux overlay bodies under `$2000` |
| `COMPILER.SYSTEM` II+ / //e / aux / Saturn | 35,027 / 34,875 / 35,699 / 36,114 | 5,677 / 5,829 / 5,005 / 4,590 | BSS ceiling `$BD00` (`$0200` stack) |
| `RUNNER.SYSTEM` II+ / //e / aux / Saturn | 31,949 / 29,537 / 30,486 / 33,143 | 8,755 / 11,167 / 10,218 / 7,561 | BSS ceiling `$BD00` (`$0200` stack) |

---

## Interpreter binary layout

The interpreter SYS image is laid out by `swiftii-system.cfg`
(`__STACKSIZE__ = $0400`, `__LCADDR__ = $D000`, `__LCSIZE__ = $2800`):

```
$0080-$0099  ZP            cc65 runtime zero page
$00D0-$00DF  SWIFTIIZP     VM hot vars (see Zero page section)
$2000-       MAIN image    STARTUP+LOWCODE+CODE+RODATA+DATA+INIT+ONCE;
                           the file also carries the LC segment past
                           this point (copied to the language card at
                           boot)
       ...   BSS           starts at the ONCE watermark, grows up to
                           $BB00 ($BF00 − __STACKSIZE__): value stack,
                           call frames, parser locals, name tables, the
                           shared bytecode buffer, and the heap
$BB00-$BEFF  C stack       cc65 software stack ($0400 reserved); sp
                           grows down, stays above BSS
$BF00-$BFFF  ProDOS global page                do not touch
$C000-$CFFF  I/O space     soft switches, slot ROMs
$D000-$F7FF  LC segment    cold code (see below); copied from the file
                           to the language card once at boot
$F800-$FFFF  Monitor ROM   do not touch (LC RAM ends at $F7FF)
```

The heap (`runtime/heap.c`) and the VM software stack (`vm/vm.c`'s
`s_stack`) live inside BSS at whatever addresses ld65 picks, not at
fixed addresses. Run `make size` for the current MAIN bytes, BSS top,
and headroom under both ceilings.

### Language-card migration

"Warm but not hot" code is relocated into the language card via
`#pragma code-name("LC")` on selected files (`pratt.c`, `statements.c`,
`repl.c`, `metacmds.c`, `file_runner.c`). These run during compile or
once per session, so the cross-segment JSR cost is irrelevant to
user-perceived latency. The cc65 STARTUP code copies the LC segment from
the binary file to $D000–$F7FF and toggles the bank-select soft switches
once at boot; nothing in user-facing code needs to know which segment a
callee lives in. The hot path (vm.c dispatch, lexer, value.c, heap.c,
builtins.c, platform I/O) stays in main-RAM CODE.

Note that LC code occludes the Applesoft ROM at the same addresses, so
any ROM-bridge wrapper (the Family B `tone` / `wait` ROM calls, for
example) must bank ROM in for the duration of the call.

### Shared bytecode buffer

`src/compiler/bcbuf.c` provides one buffer shared by the REPL and the
file runner (a process is only ever one or the other, so sharing it
reclaims BSS). It is partitioned:

```
bcbuf[0 .. arena_used)               persistent function arena
bcbuf[arena_used .. arena_used + S)  top-level scratch (this compile)
```

The arena grows forward as `func` declarations are compiled; in REPL
mode `arena_used` persists across input lines, so a function defined on
one line is callable on the next. The scratch is rewritten in place on
each REPL input or each file compile. `globals_reset()` (file-runner
before each fresh compile; REPL `:reset`) calls `funcs_reset()` →
`bcbuf_arena_reset()`, returning the arena watermark to zero; a failed
compile rolls back the arena via `bcbuf_arena_truncate()`.

When the compiler finishes a function body emitted inline at the current
end-of-scratch, it calls `bcbuf_rotate_func_into_arena()` to move the
body bytes "down" into the arena and shift earlier scratch bytes "up" by
the body size. The three-reversal in-place rotation preserves bytes
verbatim: relative JUMP / JUMP_IF_FALSE / LOOP offsets stay valid
because they were emitted against PC and the rotation does not change
body-internal or scratch-internal distances; `OP_CALL` targets are
looked up by `fn_idx` at runtime (not baked as absolute addresses), so
the rotation needs no patching. The VM starts execution at
`program_start = bcbuf_arena_used()` (the arena/scratch boundary); its
`pc < len` bounds check uses `bcbuf_arena_used() + scratch_size` so
cross-region jumps are legal.

### Input layer (design doc 003)

The //+ typing-model translator (`src/platform/apple2/input.c`) is a
batch function: one call per Return-terminated line, with the state
machine on the C stack. It is compiled out of `WITH_IIE` builds so the
//e binaries carry no dead //+ input-method code; the //e binaries use
the native case path instead.

### Capability struct + launcher boot claims

`platform_capabilities_t platform_caps` lives in DATA (7 bytes,
partially initialized to a conservative pre-IIe default so screen
rendering stays safe if `osdetect_init` somehow doesn't run). Declared
in `src/platform/apple2/osdetect.h`. The lite interpreter populates only
`machine_type` from $FBB3; Saturn / aux-RAM / 80-col probes live in the
launcher and the extras-build osdetect so they never enter the lite
footprint.

The launcher claims this memory until the chained interpreter takes
over:

| Region          | Use                                                  |
|-----------------|------------------------------------------------------|
| $0080–$0086     | ZP scratch (saved byte, slot pointers, machine type) |
| $0090–$00A9     | ZP-resident bouncer (15 B) + READ/CLOSE param blocks |
| $1C00–$1FFF     | MLI I/O buffer (4 pages, page-aligned)               |
| $2000–~$21CE    | Launcher code + RODATA (overwritten by MLI READ)     |

The ZP bouncer is the key trick: MLI READ overwrites $2000+ with the
chosen interpreter's bytes, so the CLOSE + `JMP $2000` sequence that
runs *after* READ must live where READ can't reach. The bouncer's
`.word` operands point at ZP copies of the READ / CLOSE param blocks
(also pre-staged in ZP) so the param blocks survive the overwrite.

---

## Family B - Compiler + Runner layout

The Family B tools are **MAIN-only** SYS binaries (empty LC so ProDOS's
MLI body survives - the whole point of the split, design docs 015/016).
Both binaries pack BSS to within ~100-150 B of the ceiling - **buffers
and the C-stack reserve are the only currency for new code** (the
`make size` "headroom" column is *file* headroom, not free RAM).

**Current Family B stack reserve:** the Compiler and Runner both set
`__STACKSIZE__ = $0200` (512 B). The Compiler was trimmed past doc 017's
earlier `$0300` reserve to pay for `tone`, `abs` / `sgn`, and the
string-method recognizers; the Runner landed at `$0200` with the file/dir
CRUD work. BSS overlays ONCE and grows up to `$BF00 − __STACKSIZE__`, so
the C-stack reserve is a BSS lever. Both tools are `-Cl` (static locals),
so the C stack only carries the recursive-descent / dispatch-helper call
chain, not data-dependent depth.

Low RAM (shared with, and time-disjoint from, the launcher):

```
$0800-$1BFF  Launcher: editor GAPBUF region (gap buffer + screen frame;
             II+ 4 KB text + 963 B frame, //e 3 KB + 1,923 B)
$0C00-$1BFF  Compiler: 4 KB streaming source window (srcwin.c; the
             launcher's staged path at EDIT_PATH_ADDR $0C00 is copied
             out before the first source read)
$1C00-$1FFF  Fixed MLI I/O buffer (prodos.c - one open file at a time)
$2000-       SYS load address (code + rodata + data + ONCE/BSS above)
      $BD00  Compiler/Runner BSS ceiling ($BF00 − $0200 C stack)
      $BEFF  load-image ceiling (ProDOS SYS, 40,704 B from $2000)
```

Key buffer sizes (Makefile `COMPILER_DEFS`/`RUNNER_DEFS`):
Compiler - source window 4,096 (low RAM, not BSS), bytecode arena
1,834 B on flat Tier 1 — both the II+ and the //e-native (`COMPILER_IIE_DEFS`,
`WITH_IIE`) flat builds — (896 B //e-aux window, 640 B Saturn window),
const pool 768 B Tier 1 (744 B //e aux, 704 B Saturn), `MAX_GLOBALS` 48 /
`MAX_FUNCS` 24. Runner - flat `.swb` image 2,944 B on both flat builds (II+
and //e-native; sized to the largest Tier 1 `.swb`; executes bytecode in
place), runtime heap 2,136 B on II+, 2,560 B on //e (both the non-aux flat
and the aux Runner), 1,792 B on Saturn, readFile buffer
`USERFILE_READ_CAP` 512 B (also the `listDirectory` block buffer), and
`FILE_BC_SIZE` 1 (bcbuf unused). Source size is disk-bounded (the window
slides at statement boundaries); on flat Tier 1 the bytecode arena is the
practical program cap, while the aux/Saturn tiers lift total bytecode for
function-heavy programs through paged stores.

---

## SWIFTAUX aux copy-down layout

`SWIFTAUX.SYSTEM` gives a //e-with-aux machine (no Saturn) the XLC
builtin surface. Rather than spilling *data* to aux, SWIFTAUX parks XLC
*code* in aux main RAM and copies one
dispatcher body at a time down into a fixed main-RAM staging buffer,
then `JSR`s it with all aux switches off - so the body runs against the
normal main ZP / stack / data. See `swiftaux-system.cfg`,
`src/platform/apple2/aux_xlc.s`, and
`tools/host/diskimg/pack_swiftaux.py`.

| Region | Address | Notes |
|--------|---------|-------|
| MAIN image | main `$2000`–`< $BF00` | lite-flavored interpreter (core ops inline); loaded by the boot launcher. Run `make size` for the current MAIN bytes and headroom. |
| **STAGING** | main `$B000`–`$BAFF` | copy-down buffer: one XLC body runs here per call. It is the hole between `__STAGING__ = $B000` and the C-stack bottom `$BB00`; each overlay segment is capped by `__STAGEMAX__`. |
| cc65 C-stack | main `$BB00`–`$BEFF` | `__STACKSIZE__` = `$0400`; the stack stays above the staging buffer. |
| **AUX_PARK** | **aux** `$2000`–`$BFFF` | packed park image built by `pack_swiftaux.py`: a directory at `$2000` (one 4-byte entry per XLC id: offset + length, relative to the park base), followed by body-sized overlay payloads. Reached via ROM AUXMOVE (`$C311`), not `RAMRD` execution. |

The park base, directory shape, and `__STAGING__`/`__STAGEMAX__` must stay
in sync across `aux_xlc.s`, `swiftaux-system.cfg`, and `pack_swiftaux.py`
(the boot-launcher aux loader stages the park; the runtime trampoline reads
the directory entry and copies exactly that body down). AUXMOVE lives in the
//e internal `$Cxxx` ROM, so `INTCXROM` is toggled in only across each
AUXMOVE call (the loader and trampoline both restore `SLOTCXROM` so MLI's
slot-based disk driver stays reachable).

---

## Tagged value layout (3 bytes)

Every Value on the VM stack and in heap-stored containers is 3 bytes:

```
byte 0:  tag         see below
byte 1:  payload lo  little-endian
byte 2:  payload hi
```

Tag byte values:

| Tag         | Value | Meaning                                    | Payload                |
|-------------|-------|--------------------------------------------|------------------------|
| `T_NIL`     | $00   | nil                                        | unused                 |
| `T_BOOL`    | $01   | Bool                                       | 0 or 1 in low byte     |
| `T_INT`     | $02   | Int (16-bit signed)                        | the integer            |
| `T_STR`     | $10   | String (pool index < 16, else heap offset) | 16-bit                 |
| `T_ARR`     | $11   | Array (heap pointer)                       | 16-bit heap address    |
| `T_OPT_NIL` | $20   | None - alias for nil-of-known-type         | unused                 |
| `T_FN`      | $30   | Function (bytecode address)                | 16-bit bytecode addr   |

**Optional representation:** `T_NIL` and `T_OPT_NIL` are treated
identically by `IS_NIL_VAL(v)`. There is no boxing for the "some" case -
a non-nil optional is just the underlying value with its ordinary tag
(e.g. an `Int?` carrying 5 is `T_INT` with payload 5). `OP_OPT_SOME` is
therefore a no-op. `T_OPT_NIL` remains distinct from `T_NIL` only as a
typing hint for a future type-checker; the VM treats them identically.

`T_FN` remains a reserved, unused tag value.

---

## Heap layout

The heap is a bump allocator with reference counting and topmost-block
LIFO reclaim. See `docs/contributing/design/002-heap-and-strings.md` for
the rationale; this section is just the resulting layout.

Each allocation has a 4-byte header, little-endian:

```
byte 0,1:  refcount      u16, starts at 1 on alloc
byte 2,3:  payload_len   u16, byte count of the payload
byte 4..:  payload       payload_len bytes
```

The first valid header offset is `STRING_POOL_SLOTS` (16) so a `T_STR`
payload value can be discriminated against the static string pool by a
single `< STRING_POOL_SLOTS` test.

Payload formats currently in use:

- **String**: the raw bytes; `payload_len` is the byte count.
- **Array** (design doc 007): two u16s (`count`, `capacity`) followed by
  `capacity` × 3-byte tagged-Value slots. `payload_len` covers the full
  `4 + 3 * capacity` allocation; the live element range is the first
  `count` slots.

Reclaim policy:

- A `heap_release` that drops the topmost block's refcount to zero
  rewinds the bump pointer past its header and payload - the common case
  for expression-temporary strings (`print(a + b)`).
- A mid-heap drop to zero leaves dead bytes behind until the next
  `heap_reset` (file mode: between programs; REPL: only on `:reset`).

Segregated free lists and a compaction pass are not implemented. The
current introspection surface is `heap_free_bytes()`, exposed in the REPL
as `:mem`. A failed compile uses `heap_savepoint`/`heap_rollback` to
discard every compile-time allocation in one shot - see `runtime/heap.h`.

---

## Zero page

**$D0–$DF (16 bytes) is claimed by the VM.** Storage is provisioned by
`src/vm/zeropage.s` and exposed to C through `src/common/zeropage.h`
(cc65: `#pragma zpsym`; host: ordinary globals). `vm_pc` / `vm_sp` /
`vm_fp` / `vm_op` are hot in `vm.c`'s dispatch loop.

`$E0–$EF` remains **reserved** for a potential lexer asm conversion (per
design doc 008 open questions); not claimed at the linker level today.

Current breakdown:

| Address  | Width | Name        | Purpose                                |
|----------|-------|-------------|----------------------------------------|
| $D0-$D1  | 2 B   | `vm_pc`     | VM program counter                     |
| $D2      | 1 B   | `vm_sp`     | VM software stack pointer (32-slot)    |
| $D3      | 1 B   | `vm_fp`     | VM frame pointer (for locals)          |
| $D4-$D5  | 2 B   | `vm_tmp1`   | handler scratch pointer 1              |
| $D6-$D7  | 2 B   | `vm_tmp2`   | handler scratch pointer 2              |
| $D8-$D9  | 2 B   | `heap_ptr`  | heap bump pointer mirror               |
| $DA      | 1 B   | `vm_op`     | current opcode being dispatched        |
| $DB      | 1 B   | `vm_flags`  | dispatch flags (halt, error)           |
| $DC-$DD  | 2 B   | `vm_tmp3`   | call-frame scratch                     |
| $DE-$DF  | 2 B   | (spare)     | unallocated within the $D0–$DF claim   |
| $E0-$EF  | 16 B  | (reserved)  | lexer asm if and when                  |

Source of truth: `src/vm/zeropage.s` (cc65 storage) and
`src/common/zeropage.h` (C declarations). `vm_sp`, `vm_fp`, `vm_op`, and
`vm_flags` are single bytes, not 2-byte slots - keep that in mind before
adding new variables.

ProDOS reserves $00-$BF for itself and applications. The cc65 runtime
claims roughly 26 bytes near the bottom of zero page for its software
stack pointer (`sp`), pointer registers (`ptr1`-`ptr4`), and temporary
scratch (`tmp1`-`tmp4`, `regsave`). Anything above $EF is fair game for
ProDOS, BASIC.SYSTEM, and other co-resident code - do not stash anything
there.

---

## Boundaries between regions

Invariants the linker statically enforces (every data region lives
inside BSS or RODATA), restated so they survive future hand layout:

- The VM stack and the heap must not grow into each other.
- Function bytecode and program bytecode share one buffer in REPL mode;
  the boundary is tracked dynamically. Collision → OOM.
- The ProDOS global page ($BF00–$BFFF) and, for Family B, the MLI body
  beneath an empty LC must never be touched while resident.
- BSS must never cross its ceiling (`$BF00 − __STACKSIZE__`); `make size`
  is the gate.

---

## When this file changes

Any change to the layout above requires:

1. Updating the relevant ld65 linker config under
   `src/platform/apple2/` to match.
2. Updating the relevant constants in `src/common/config.h`.
3. Running `make size` to confirm the new layout fits.
4. A short note in `LESSONS.md` if you discovered a hardware constraint
   that drove the change.

Beyond ad-hoc edits, **this file must be re-derived on every release
build** (`make release` / a version bump), on top of refreshing
[`FEATURES.md`](../using/FEATURES.md): re-check every buffer size, BSS
ceiling, stack reserve, zero-page slot, and heap / LC address against the
actual build so the map never carries stale numbers forward. See the
"Cutting a release" checklist in [`AGENTS.md`](../../AGENTS.md).
