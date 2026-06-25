# 011 - Extras code in Saturn bank / IIe aux LC

As built, extras code uses a generic XLC dispatch path: one MAIN
trampoline, one JMP table at XLC offset 0, and one host-shim switch case per
builtin. That keeps new XLC builtins to roughly the parser-branch cost in
MAIN, instead of one trampoline per builtin. `SWIFTSAT` proved the mechanism;
`SWIFTAUX` extends the same model to //e aux memory.

## Problem

SWIFTIIX is at 40,690 / 40,704 B (14 B headroom) and Phase 9
features (`asc`/`chr` ~550 B, `Int(s)` ~470 B, `struct` ~1-2 KB,
`switch` ~1-2 KB) total ~5-10 KB of new code budget. Phase 10
(`home`/`htab`/`vtab`) adds ~600-750 B more. The earlier
"load LC bytes directly to built-in LC" approach failed because ProDOS's MLI
body lives in built-in LC bank 1; MLI can't write to the LC region without
destroying its own running code. A budget sweep can free ~200-500 B; not
enough.

SWIFTIIX runs only on machines with ≥128 KB total RAM. The "extras"
RAM beyond the standard 64 KB takes one of two forms:

- **Saturn 128K** card (Apple II / II+): 8 banks of 16 KB at
  `$D000-$FFFF`, bank-switched via the card's slot soft switches.
  Bank 0 maps onto the built-in LC; banks 1-7 are extras. So
  Saturn has up to **112 KB of extra LC-space** beyond the standard
  16 KB built-in LC, all bankable in independently.
- **IIe extended 80-column card** (Apple IIe): 16 KB aux LC at
  `$D000-$FFFF` under `ALTZP`, plus 48 KB aux main at `$0200-$BFFF`
  under `RAMRD`/`RAMWRT`. So IIe-aux has **16 KB of extra LC-space**
  (under `ALTZP`), separate from the built-in LC.

Both can host ~10 KB of extras code without touching the
ProDOS-resident built-in LC. The mechanism differs - Saturn uses
slot soft switches and doesn't affect zero page or stack; IIe aux's
`ALTZP` swaps zero page + stack + LC all at once, which complicates
any code that touches main-RAM ZP variables while aux LC is
selected.

## Proposal

Add an **extras-LC region** to SWIFTIIX's address space, separate
from cc65's existing LC code:

- Built-in LC bank 2 (`$D000-$F7FF`, ~10 KB): standard cc65 LC code
  (parser, REPL driver, compiler tables) - unchanged from today's
  SWIFTIIX.
- Extras-LC (`$D000-$FFFF` in Saturn bank 1 OR IIe aux LC, ~12 KB):
  Phase 9 + Phase 10 extras code (asc/chr, struct dispatch, switch
  jump tables, home/htab/vtab, etc.).

The boot launcher loads two LC blobs: cc65's standard LC code into
built-in LC bank 2 (via cc65's standard startup memcpy), and the
extras code into the platform's extras-LC region (via a new boot
launcher stage).

Phase 9+ extras-only code is gated by `WITH_EXTRAS` *and* placed in
a new ld65 segment `XLC` (extras-LC). At runtime, calls into `XLC`
functions go through a small bank-switch trampoline: switch the LC
window to extras-LC, JSR into `$D000+`, switch back to built-in LC.

**Two binary variants** address the Saturn-vs-aux split per the
ROADMAP section "Open: split SWIFTIIX into Saturn-vs-aux variants?"
trigger criteria:

- **`SWIFTSAT.SYSTEM`** (15 chars) - for Apple II / II+ with
  Saturn 128K. Saturn switches don't affect ZP or stack; the
  bank-switch trampolines are simple (~10 B each).
- **`SWIFTAUX.SYSTEM`** (15 chars) - for Apple IIe with 64K
  extended 80-col. `ALTZP` swaps ZP + stack, so trampolines need a
  careful copy of the few ZP slots that cross the boundary (~25-40
  B each, plus shared-stack discipline).

Stage 1 (this design doc cycle) **ships SAT only**. AUX is a
follow-up. During stage 1, IIe-aux-only machines fall back to the
existing legacy `SWIFTIIX.SYSTEM`. The boot launcher probes both
signals at boot and routes accordingly:

```
detection result               → ships
Saturn present (any machine)   → SWIFTSAT.SYSTEM (Phase 9 extras)
IIe aux only, no Saturn        → SWIFTIIX.SYSTEM (stage 1 legacy)
                                  SWIFTAUX.SYSTEM (stage 2)
no extras RAM                  → SWIFTIIL.SYSTEM (lite)
```

On a hybrid machine (Saturn + IIe aux both present), Saturn wins -
its switching architecture is mechanically cleaner.

After stage 2 ships, `SWIFTIIX.SYSTEM` retires; the four disk
binaries become `SWIFTII.SYSTEM` (boot launcher), `SWIFTIIL.SYSTEM`,
`SWIFTSAT.SYSTEM`, `SWIFTAUX.SYSTEM`.

## Detailed design

### File format (SWIFTSAT.SYSTEM)

The packed file has three chunks:

```
offset 0           : 6-byte header
                     [main_size_lo, _hi,
                      lc_size_lo,   _hi,
                      xlc_size_lo,  _hi]
offset 6           : main image       (main_size B; loads to $2000)
offset 6+main      : LC image         (lc_size B;   loads to built-
                                       in LC bank 2 at $D000+)
offset 6+main+lc   : XLC image        (xlc_size B;  loads to
                                       Saturn bank 1 at $D000+)
```

ld65 produces three outputs (`%O.main`, `%O.lc`, `%O.xlc`); a
post-link script packs them with the 6-byte header. Total file size
ceiling for the on-disk SYS file is now effectively limited by what
the boot launcher can READ via MLI (no ProDOS-imposed ceiling on the
file size - only the loader's per-target RAM budgets matter).

### Linker config

`src/platform/apple2/swiftsat-system.cfg`:

```
MEMORY {
    ...
    MAIN: file = "%O.main", start = $2000, size = $BF00 - $2000;
    LC:   file = "%O.lc",   start = $D000, size = $2800;
    XLC:  file = "%O.xlc",  start = $D000, size = $3000;
    BSS:  file = "",        start = __ONCE_RUN__, size = $BF00 - ...;
}
SEGMENTS {
    ...
    CODE:  load = MAIN, type = ro;
    LC:    load = LC,   run = LC,  type = ro;   /* built-in LC bk-2 */
    XLC:   load = XLC,  run = XLC, type = ro;   /* Saturn bank 1 */
    BSS:   load = BSS,  type = bss;
}
```

Both `LC` and `XLC` run at `$D000`. The CPU sees one or the other
based on the active LC switch (built-in LC bank-2 read = `$C080`;
Saturn bank-1 read = a Saturn-specific switch determined by the
detected slot, baked into the trampoline at install time).

A new compiler attribute / pragma marks SWIFTIIX-extras functions
for placement in `XLC`. Concretely, we compile the relevant `.c`
files with `--code-name XLC` (same mechanism used today for
`pratt.c` going into `LC`). Files to relocate:

- `src/compiler/asc_chr.c` (new, Phase 9)
- `src/compiler/struct.c` (new, Phase 9)
- `src/compiler/switch.c` (new, Phase 9)
- `src/platform/apple2/text_extras.c` (new, Phase 10 - home/htab/
  vtab implementations)

Existing files (vm.c, builtins.c, lexer.c, etc.) stay in CODE/LC as
today.

### Boot launcher changes (SAT path)

The boot launcher today opens a single SYS file and issues one MLI READ
to `$2000+`. For SWIFTSAT it does:

1. Open `SWIFTSAT.SYSTEM`.
2. MLI READ 6 B (header) into ZP scratch.
3. MLI READ `main_size` B into `$2000+`. (MLI still alive in
   built-in LC bank 1.)
4. MLI READ `lc_size` B into a main-RAM scratch buffer (e.g.,
   `$0200-$0FFF`, ~3.5 KB at a time if needed in chunks). Switch
   LC to built-in bank 2 write-enable; memcpy scratch → `$D000+`.
   Switch back to built-in bank 1 read for next MLI call.
5. Repeat step 4 in chunks until `lc_size` bytes copied to
   built-in LC bank 2.
6. MLI READ `xlc_size` B into the same main-RAM scratch buffer.
   Switch to Saturn bank 1 write-enable; memcpy scratch →
   `$D000+`. Switch back to built-in bank 1 read.
7. Repeat step 6 in chunks until `xlc_size` bytes copied to Saturn
   bank 1.
8. MLI CLOSE.
9. Final switch: built-in LC bank 2 read (cc65 startup expects
   this). Saturn bank 1 is loaded but not active.
10. JMP `$2000`.

Step 4 may seem unnecessary (cc65's standard crt0 normally copies
LC bytes from MAIN to built-in LC bank 2 itself), but in this
design the LC bytes never enter MAIN - they go from disk to scratch
to LC directly. We disable cc65's built-in LC copy via the verified
`load = LC, run = LC` linker layout.

### Boot launcher size estimate

The chunked-staging bouncer needs: header read, three loops
(main / LC / XLC) with READ + memcpy + switch sequences. Estimated
~150-250 B of ZP / main-RAM bouncer code. Boot launcher today is
1,854 / 4,096 B; ample room.

The ZP layout reshuffle from the failed LC-direct-loader attempt is
re-applicable here.

### Runtime bank switching (SAT)

Calls from main code into XLC functions go through a small
trampoline. Two strategies:

**Strategy A - manual trampoline at call sites**:

```c
extern int sat_xlc_call(void (*fn)(void), int arg);
```

cc65 indirect call via the trampoline. Each call site: ~3-5 B
(JSR sat_xlc_call). Trampoline itself: ~20 B. Total per call: ~5 B
+ ~20 cycles overhead.

**Strategy B - segment-aware compiler emit**:

The compiler emits a one-byte "needs XLC bank" prefix before
function pointers that point into XLC. The VM's call dispatch
switches banks based on this prefix. More code in the VM; cheaper
per call.

**Recommend Strategy A** for stage 1 (simpler, no VM changes). If
profiling shows XLC calls are a hotspot, switch to Strategy B in a
follow-up.

The trampoline (Strategy A, Saturn slot N):

```asm
sat_xlc_call:
    sta   target_lo            ; arg-passing via cc65 convention
    stx   target_hi
    bit   $C0XX                ; select Saturn bank 1 read
    jsr   xlc_indirect          ; into XLC at $D000+
    bit   $C081                ; back to built-in LC bank 2 read
    rts
xlc_indirect:
    jmp   (target_lo)
```

~25 B for the trampoline. One per binary.

The exact Saturn switch address depends on the detected slot (boot
launcher already records `g_saturn_slot`). The trampoline is built at
boot time with the correct switch baked in (5-line install-time
patch).

### Runtime bank switching (AUX, stage 2)

The aux path does **not** use the aux language card / `ALTZP`. An early
sketch flipped `ALTZP` on (which also swaps ZP + stack to aux) and
saved/restored the cc65 ZP slots per call; it was abandoned - see "Why
not the aux LC" below. Stage 2 instead parks XLC bodies in aux main RAM
and copies them down into a main-RAM staging buffer to run, never
touching `ALTZP` - see "Stage 2 refresh - SWIFTAUX" below for the built
mechanism.

### MLI safety throughout the boot launcher

The chunked-staging approach keeps MLI's body in built-in LC bank 1
intact throughout. Steps that touch the bank:

| Step                              | LC bank active           |
|-----------------------------------|--------------------------|
| OPEN / READ header                | Built-in bank 1 (MLI)    |
| READ main → $2000                 | Built-in bank 1 (MLI)    |
| Switch + memcpy → built-in bank 2 | Built-in bank 2 (write)  |
| Switch back for next MLI          | Built-in bank 1 (MLI)    |
| Switch + memcpy → Saturn bank 1   | Saturn bank 1 (write)    |
| Switch back for next MLI          | Built-in bank 1 (MLI)    |
| CLOSE                             | Built-in bank 1 (MLI)    |
| Final: built-in LC bank 2 read    | Built-in bank 2 (cc65)   |

MLI never sees its body unmapped. CLOSE works normally - no leaked refnum,
unlike the failed LC-direct-loader approach.

### Build matrix

Filenames (ProDOS 15-char max, alphanumeric + dot only):

| Role                          | Filename             | Status              |
|-------------------------------|----------------------|---------------------|
| Boot launcher (auto-launched)     | `SWIFTII.SYSTEM`     | unchanged           |
| Lite interpreter              | `SWIFTIIL.SYSTEM`    | unchanged           |
| Saturn extras (this doc)      | `SWIFTSAT.SYSTEM`    | new (stage 1)       |
| Aux extras                    | `SWIFTAUX.SYSTEM`    | new (stage 2)       |
| Legacy unified extras         | `SWIFTIIX.SYSTEM`    | kept for stage 1; retires after stage 2 |

After stage 1 ships:

| Target                          | Binary             | Detection                |
|---------------------------------|--------------------|--------------------------|
| Any machine with Saturn 128K    | `SWIFTSAT.SYSTEM`  | `g_saturn_slot != $FF`   |
| IIe extended 80-col, no Saturn  | `SWIFTIIX.SYSTEM`  | `g_aux_found` only       |
| 64K, no extras                  | `SWIFTIIL.SYSTEM`  | both detection flags = 0 |

On a hybrid machine (Saturn + IIe aux both present), Saturn wins -
its switching architecture is cleaner. Routing logic in the boot
launcher checks Saturn first, then aux.

After stage 2 ships:

| Target                          | Binary             | Detection                |
|---------------------------------|--------------------|--------------------------|
| Any machine with Saturn 128K    | `SWIFTSAT.SYSTEM`  | `g_saturn_slot != $FF`   |
| IIe extended 80-col, no Saturn  | `SWIFTAUX.SYSTEM`  | `g_aux_found` only       |
| 64K, no extras                  | `SWIFTIIL.SYSTEM`  | both detection flags = 0 |

`SWIFTIIX.SYSTEM` (legacy unified extras) retires when stage 2
ships, leaving four on-disk binaries: `SWIFTII.SYSTEM`,
`SWIFTIIL.SYSTEM`, `SWIFTSAT.SYSTEM`, `SWIFTAUX.SYSTEM` - all
exactly 15 chars.

### Phase 9 features land on SAT first

Per ROADMAP priority order, the items that land on SAT are:

1. `Int(s)`, `asc(s)`, `chr(n)` - extras-LC code, ~700 B total.
2. `struct` - extras-LC code, ~1.5-2 KB. (Compile-time logic;
   the runtime struct ops are tiny.)
3. `switch` - extras-LC code, ~1.5-2 KB. (Jump-table compilation.)
4. Array methods - small, fit in main CODE or extras-LC.
5. `if let else` - small, fit in main CODE.

IIe-aux owners initially keep the current SWIFTIIX feature set
(no Phase 9 extras). They get the upgrade when `SWIFTAUX.SYSTEM`
ships in stage 2.

## Alternatives considered

**Single SWIFTIIX binary with runtime-dispatched bank switches.**
Every XLC call branches on platform (Saturn vs aux). ~5-10 B
overhead per call. Adds ~80-150 B of branch code. Workable but
ugly, and aux's `ALTZP` complexity intrudes on every call site.
**Rejected** - clean separation between SAT and AUX binaries is
worth the duplicated build complexity.

**Put extras DATA (bytecode, heap, strings) in aux, code stays in
main.** This frees BSS budget instead of code budget. ~6-10 KB of
BSS could move. Doesn't directly help asc/chr (CODE-bound), but
helps `struct` (which needs heap for struct instances). **Defer**
to a future doc; complementary to this one, not a substitute.

**Saturn bank 0** (the LC-compatibility slot) **instead of bank 1**.
Bank 0 emulates a standard 16 KB LC and may conflict with built-in
LC state on some Saturn clones. Bank 1 is the conventional "first
extras" bank and is independently selectable across Saturn variants.
**Rejected** in favour of bank 1; banks 2-7 remain reserved for
future use (see open question 3).

**Skip Saturn entirely; just put extras code in built-in LC bank 1
after ProDOS is no longer needed.** SwiftII does call MLI at
runtime today (`file_runner` mode opens a `.SWIFT` source file via
ProDOS). Destroying ProDOS in bank 1 would either kill file mode
or require refactoring file I/O out of SwiftII (e.g., into the
boot launcher or into the Phase 14 editor). The editor being a
separate program reduces but doesn't eliminate SwiftII's runtime
file-I/O need. **Rejected** for stage 1 in favour of preserving
ProDOS for `file_runner` and as a future option for the Phase 14
editor's chain-back interaction. Revisit if/when SwiftII's
architecture changes such that ProDOS is no longer needed at
runtime.

**Just budget sweep.** The measured budget sweep only finds
200-500 B. Not enough.

## Cost

- **Memory cost (SAT binary)**:
  - Main CODE: same as today (~30 KB) - extras code moved out.
  - Built-in LC bank 2: same as today (~10 KB) - standard cc65 LC.
  - Saturn bank 1: ~10-12 KB available, ~0-5 KB used initially
    (Phase 9 + 10 extras). Headroom for `struct` / `switch` /
    future Phase 12 80-col extras.
  - Saturn banks 2-7: ~84 KB reserved for future use; not touched
    in stage 1.
- **Memory cost (boot launcher)**: +150-250 B for chunked-staging
  bouncer. From 1,854 / 4,096 → ~2,050 / 4,096 (2,046 B headroom).
- **Runtime cost**: 20-30 cycles per XLC call (bank switch + JSR +
  bank switch back). Negligible for compile-time calls; small but
  measurable for runtime calls. The features going into XLC are
  mostly compile-time (`struct` declarations, `switch`
  case-compilation) or rare-runtime (`asc`/`chr`/`Int(s)` - called
  in user programs, not in tight loops).
- **Code complexity**: medium-high. Two new pieces:
  - Boot launcher gains the chunked-staging path with Saturn bank
    selection.
  - cc65 build pipeline gains a `--code-name XLC` step for
    extras-only source files.
  - Linker config has three output files instead of one.
  - Pack script combines the three.
- **Schedule cost**: estimated **4-6 sessions** for stage 1 SAT:
  - Session 1: linker config, pack script, build matrix. ✅ commit 1
  - Session 2: boot launcher chunked staging + Saturn bank switch. ✅ commit 2
  - Session 3: trampoline + first XLC-resident function (asc).
    Split into 3a (trampoline + smoke fn returning $42) ✅, 3b
    (asc end-to-end, smoke fn retired) ✅ 2026-05-28, 3c
    (dispatcher-in-XLC refactor recovering MAIN headroom) ✅
    2026-05-29, and 3d (generic dispatch - one trampoline +
    JMP table at XLC offset 0) ✅ 2026-05-29.
  - Session 4: full Phase 9 builtins via XLC (chr first; lands
    cheap under the 3d mechanism).
  - Session 5: real-hardware verification (Mariani Saturn config).
  - Session 6: docs + cleanup.
- AUX stage: another 4-6 sessions when triggered.

## Migration

- **Boot launcher**: routing logic gains a Saturn-present-→-
  `SWIFTSAT.SYSTEM` rule, checked before the existing aux/lite
  branches. Lite + legacy-extras (`SWIFTIIX.SYSTEM`) paths stay
  during stage 1.
- **Existing `SWIFTIIX.SYSTEM`**: stays in build, stays on disk
  during stage 1 (covers IIe-aux fallback). Retires in stage 2.
- **Existing host tests**: unaffected - all host code is the same
  C, just compiled into different cc65 segments.
- **No bytecode change**.

## Resolved (locked decisions 2026-05-27)

1. **Q1 - Should extras code go in built-in LC bank 1 (ProDOS) or
   Saturn/aux?** → **Saturn/aux (this doc's proposal).** Reason:
   SwiftII's `file_runner` still uses MLI at runtime for file mode;
   destroying ProDOS in bank 1 would either kill file mode or
   force an architectural refactor that's out of scope for Phase 9.
   The Phase 14 editor being a separate program reduces but doesn't
   eliminate this dependency. Revisit if SwiftII's runtime
   architecture changes such that ProDOS is no longer needed.

2. **Q2 - Which Saturn bank?** → **Bank 1.** Reason: conventional
   "first extras" bank, mechanism known to work across Saturn
   variants. Bank 0 is the LC-compatibility slot (potential clone
   variation); banks 2-7 reserved for future use.

3. **Q3 - Multi-bank Saturn use later?** → **Yes; reserve banks
   2-7.** Stage 1 uses bank 1 only. Banks 2-7 (~84 KB) earmarked
   for future phases: more extras code (Phase 12/13), enlarged
   heap, source buffer for editor integration. Document in ROADMAP
   without committing to specifics.

4. **Q4 - Aux-only (no Saturn) Phase 9 path?** → **Stage 1 SAT only;
   AUX is stage 2.** IIe-aux-only machines keep current
   `SWIFTIIX.SYSTEM` during stage 1 - they don't lose anything,
   just don't gain Phase 9 features. Stage 2 adds
   `SWIFTAUX.SYSTEM` and retires `SWIFTIIX.SYSTEM`.

5. **Q5 - Bytecode buffer growth?** → **Defer.** Phase 9 features
   don't need it. The freed MAIN headroom is real but better
   allocated at Phase 9 close once we know what other budgets need
   attention. Documented in `make size` reporting; revisit at
   Phase 9 close commit or Phase 10 start.

6. **Q6 - SAT+AUX hybrid machine?** → **Saturn priority.** A IIe
   with both a Saturn card and 64K aux 80-col card boots
   `SWIFTSAT.SYSTEM`; aux RAM is unused. Reason: Saturn's
   switching architecture is mechanically cleaner (doesn't touch
   ZP/stack); hybrid machines are rare; aux being unused is fine.
   Two lines of boot launcher logic.

## Decision

Stage 1 locked `SWIFTSAT.SYSTEM` for Saturn extras. Stage 2 locked
`SWIFTAUX.SYSTEM` for //e extended 80-column aux machines, using
`ALTZP`-aware trampolines.

---

# Stage 2 refresh - SWIFTAUX (Phase 11)

SWIFTAUX uses lite-flavored MAIN plus aux copy-down for cold XLC bodies,
not a SWIFTSAT clone. Lite MAIN has little headroom and per-call AUXMOVE on
hot ops is too slow on a 1 MHz //e, so hot/core ops run inline in MAIN and
only cold bodies are copied down. STRIDE is $0800, STAGING is $B000,
AUX_PARK is aux $2000, and sparse park slots (`slot = id -
BUILTIN_XLC_FIRST`) avoid a runtime offset table.

The built path uses `swiftaux-system.cfg`, `pack_swiftaux.py`,
`aux_xlc.s`, boot-launcher aux routing, and the `make apple2-swiftaux` /
`make disk-aux` targets. It ports helper-free user builtins to per-body
overlays and groups the platform builtins into copy-down overlays whose
entry switches on `xlc_builtin_id`. Two walls remain: the platform parser
table is MAIN-resident, and some platform helpers need runtime data, forcing
eviction of additional cold inline opcode bodies
(`str_concat`/`new_array`/`arr_len`). The GR group body (2657 B) overflowed
the fixed 2 KB stride, so the **park was redesigned
from fixed-stride to a packed directory**: a 24-entry `[off,len]` table
(one per builtin id $0D-$24; grouped ids share an entry, which subsumes the
id→slot map) followed by the bodies concatenated with no padding. The
trampoline reads `dir[id-FIRST]` (a 4-byte AUXMOVE) then copies exactly
`len` body bytes to STAGING. This removes the stride ceiling (a body may be
any size up to the STAGING..C-stack hole `__STAGEMAX__`), makes copies
body-sized (the evicted hot `str_concat` copies ~200 B, not a 2 KB stride),
and shrank `SWIFTAUX.SYSTEM` ~15 KB (no padding). MAIN 40,491 B / 213 B
headroom; lite (115) + SWIFTSAT (240) byte-identical. Q-A3 resolved (keep
140 KB two-image). Slice 3 surface complete pending emulator verification.
See LESSONS 2026-05-31.
**Supersedes the
"Runtime bank switching (AUX, stage 2)" sketch above** - that sketch
assumed extras code would live in the *aux language card*
(`$D000-$FFFF` aux, gated by `ALTZP`) and that the trampoline would
save/restore the cc65 ZP slots across each `ALTZP` flip. Phase 11
abandons the aux LC entirely (see "Why not the aux LC" below) in
favour of **aux main RAM (`$0200-$BFFF`) + per-call copy-down**, which
never touches `ALTZP` at all.

**Two decisions locked by the user 2026-05-30:**

1. **Execution model = aux main RAM + copy-down staging.** XLC bodies
   are parked in aux main RAM (`$0200-$BFFF`, reached via `RAMRD` /
   `RAMWRT`, *not* `ALTZP`). To run one, copy it down into a fixed
   **main-RAM staging buffer** and `JSR` it with `RAMRD`/`RAMWRT` *off* - so the body executes against the normal main ZP, stack, and BSS,
   exactly like a MAIN-resident function. The aux switches are touched
   only during the block copy, never during execution.
2. **Binary = separate `SWIFTAUX.SYSTEM`.** A fourth on-disk binary,
   peer to `SWIFTSAT.SYSTEM`. Boot launcher probes aux and chains to it.

## Why not the aux LC (the in-place execution trap)

SWIFTSAT executes XLC bodies *in place* at `$D000` in Saturn bank 1.
That works because the Saturn bank switch only remaps `$D000-$FFFF`;
the ZP, the stack, and all of main RAM (`$0200-$BFFF` - where
`s_stack[]`, the heap, and the rest of BSS live) are untouched, so a
cc65 dispatcher reads and writes its data normally while running from
the bank.

Aux main RAM has no equivalent in-place option. To make the CPU
*fetch instructions* from aux you must turn `RAMRD` on - but `RAMRD`
redirects **every** read in `$0200-$BFFF` to aux, including the
dispatcher's loads of `s_stack[]`, `vm_sp`, heap cells, and string
data, all of which live in *main* `$0200-$BFFF`. The body would
execute but read garbage. The aux *language card* (`$D000` aux) avoids
the data-aliasing problem for `$0200-$BFFF`, but it is gated by
`ALTZP`, which swaps the ZP and the 6502 stack to aux - so a cc65
dispatcher would see aux ZP (wrong pointer values) and an
uninitialised aux stack. Both in-place routes therefore require either
heroic ZP/stack discipline or a body that touches no main RAM at all.

Copy-down sidesteps all of it: the body runs from main RAM with all
switches off, so ZP, stack, and data are the normal main ones. The
price is a block copy per call and a permanent main-RAM hole the size
of the staging buffer (not the size of the whole XLC image).

## The central risk: position independence of copied-down code

cc65 emits position-**dependent** code: a function only runs correctly
at the address ld65 linked it for (absolute `JSR`/`JMP` to its own
labels, absolute `RODATA` references). Copy-down requires the staging
address to **equal** each body's link address. Three ways to satisfy
that, none free:

- **(A) One staging buffer sized to the whole XLC image; copy the
  entire blob down once at startup and run it there forever.**
  Rejected: there is no 6-12 KB free executable hole in MAIN at
  runtime (if there were, XLC wouldn't exist). Copying the whole blob
  *per call* instead is far too slow (6-12 KB memcpy each call).

- **(B) One small staging buffer sized to the largest single
  dispatcher; copy one body per call, indexed by an `(offset,len)`
  table.** This is the cheap-RAM option (a ~256-512 B hole), but it
  needs each body to be position-independent, because they all run at
  the *same* staging address. Options to get PIC bodies:
  - **(B1)** Hand-write the aux dispatchers in position-independent
    ca65 (relative branches only; calls to MAIN helpers - `heap_*`,
    `array_*`, `s_stack` - via absolute addresses, which is fine since
    MAIN is always mapped; no in-body `RODATA`). Cost: a *second*
    implementation of ~24 dispatchers alongside the C ones used by
    SAT/host. High write + maintenance cost.
  - **(B2)** Per-body ld65 links: compile each dispatcher in isolation
    with `run = <staging>`, resolving shared helpers as imports from
    the MAIN symbol table, emit one relocatable blob + an
    `(offset,len)` entry each. Keeps the C dispatchers, but the build
    machinery is heavy and bodies that call *other* XLC bodies (e.g. a
    dispatcher → `xlc_asc` worker) must be co-located in one blob.

- **(C) Give up on running extras *code* from aux; spill extras
  *data* instead** (heap / strings / large arrays into aux, accessed
  via bulk copy-in/out at defined points). This is the "extras DATA in
  aux" alternative already listed above. It frees BSS, not CODE, so it
  does **not** hand aux owners the Phase 9/10 XLC builtin surface - it
  is a different feature. Listed here only as the fallback if (B)
  proves uneconomical.

**Recommendation: spike (B) before committing.** Slice 0 ports exactly
*one* dispatcher (`asc`, the simplest) to the aux copy-down path and
proves it end-to-end on the sim + emulator. Only if the spike is clean
do we port the rest. If (B1)/(B2) both prove too costly, fall back to
(C) or defer SWIFTAUX and keep aux-only machines on lite (see routing
fix below). This ordering keeps the expensive ~24-dispatcher port
behind a proven mechanism.

## Latent gap this phase must also close (routing)

Today's boot launcher routes **aux-only machines to `SWIFTSAT.SYSTEM`**
on the rationale (in `boot_launcher.c`) that "its XLC is empty and the
loader only switches built-in LC banks." That rationale is now stale:
SWIFTSAT's XLC holds real builtins (asc/chr/Int/array methods/home/
peek/poke/gr/…). On an aux-only machine `g_saturn_slot == $FF`, so
`xlc_init` no-ops, the loader's Saturn bank writes go nowhere, and any
XLC builtin call bank-switches a non-existent card → almost certainly
a crash. So **extras on aux-only machines is currently broken/
untested.** Phase 11 closes this either by shipping a working
SWIFTAUX, or - as an immediate safety step independent of the
mechanism work - by routing aux-only machines to **lite** until
SWIFTAUX lands (safe, just no extras). Slice 1 makes that routing
correct.

## Loader / boot-launcher changes (AUX path)

Mirror `a_install_and_chain_swiftsat` + the `LOADER` segment, but:

- **Park target = aux main RAM**, not Saturn bank 1. The chunked
  staging loop reads each XLC chunk into the `$0800` main scratch
  (unchanged), then copies it to the aux park address via `RAMWRT` on
  (`$C005`) + plain `STA` loop, `RAMWRT` off (`$C004`) - or via the
  ROM `AUXMOVE` ($C311). No `ALTZP`, no LC bank switching for the XLC
  copy. MLI's body in built-in LC bank 1 stays mapped throughout, same
  as SAT.
- **Pick the aux park base.** Candidate: high aux main RAM
  (`$Bxxx`-down) so it stays clear of the `$0200-$07FF` aux text/ZP
  pages and any aux region 80-col mode will want in Phase 12. Exact
  base is an open question (below).
- **Stash an "aux mode" marker** for `xlc_init` to read at main()
  entry (peer to `SX_SAT_SLOT` at `$1B04`), so the runtime trampoline
  knows it is the aux variant.

## Runtime trampoline (AUX path)

A new `aux_xlc_call(id)` peer to `call_xlc_dispatch`:

1. id → `(offset,len)` via a small table (built from the per-body
   sizes the linker/pack step emits).
2. `RAMRD`+`RAMWRT` on → copy `len` bytes from `aux_park+offset` to
   the fixed `STAGING` buffer → `RAMRD`+`RAMWRT` off. (Or `AUXMOVE`.)
3. `lda xlc_argc` (same argc transport as SAT).
4. `JSR STAGING`.
5. `RTS` (no bank to restore - switches are already off).

The id→table math and the copy run in always-mapped MAIN; only the
`JSR STAGING` body runs the extras code, against normal ZP/stack.

## Build matrix

- New `SWIFTAUX.SYSTEM` target in the Makefile, peer to
  `apple2-swiftsat`. Reuses the C dispatchers in `builtins_xlc.c` *iff*
  copy-down can run cc65 code (option B2); otherwise the aux build
  pulls in PIC asm dispatchers (option B1) gated by a new
  `WITH_SWIFTAUX` define.
- New `swiftaux-system.cfg`: same MAIN/LC layout as SWIFTSAT; the XLC
  segment's `run =` address is the **staging** address (option B) or
  the whole-blob main hole (option A), *not* `$D000`.
- `pack_swiftaux.py` (or extend `pack_swiftsat.py`) to emit the
  `(offset,len)` body table the aux trampoline needs.
- Disk: four 40 KB binaries overflow the 140 KB 5.25" template (same
  constraint that dropped legacy SWIFTIIX from disk), so each image
  carries ONE extras binary - SWIFTAUX *replaces* SWIFTSAT on the aux
  disk. **Settled** (see Q-A3 below): two 140 KB images, one extras binary
  each; no unified single-disk image.

## Slice plan

- **Slice 0 - mechanism spike.** Port `asc` only to aux copy-down;
  prove on sim + emulator (//e + extended 80-col). Decide B1 vs B2 vs
  fall back. *Gate for the rest of the phase.*
- **Slice 1 - detection + routing fix.** Make aux-only machines route
  safely (to lite until SWIFTAUX ships, or to SWIFTAUX once slice 0
  proves out). Host-testable.
- **Slice 2 - SWIFTAUX build target + loader.** cfg, pack tool,
  chunked staging into aux park, `xlc_init` aux arm.
- **Slice 3 - port the remaining dispatchers** behind the proven
  mechanism; size/budget pass.
- **Slice 4 - emulator verification + docs.** Retire legacy
  `SWIFTIIX.SYSTEM` per the stage-1 plan once aux is covered.

## Resolved questions

- **Q-A1 - Mechanism after the spike.** B1 (PIC ca65 dispatchers, high
  port cost, tiny RAM) vs B2 (per-body ld65 links, keep C, heavy
  build) vs fall back to data-spill / defer. Decide at the slice-0
  gate, informed by the spike's actual numbers.
  → **RESOLVED 2026-05-30: B2 = GO**, realized as **single-link ld65
  overlays** (not separate per-body link invocations). The slice-0 spike
  ported `asc` (worker + dispatcher, 314 B) to its own `XLCASC` segment
  via a `WITH_SWIFTAUX`-keyed `#pragma code-name` and placed it in an
  `OVLASC` overlay memory area at `run = $B000` (STAGING) in
  `swiftaux-spike.cfg` - one ld65 link, so MAIN helper symbols resolve to
  their real addresses while the body's own labels resolve to STAGING. Two
  proofs (no emulator needed):
  - *Structural (da65 of `SWIFTAUX.ovl.asc`)*: the dispatcher→worker call
    is `jsr $B000` (STAGING); `value_release`/`string_pool_get`/`heap_*`
    and all cc65-runtime helpers target MAIN addresses (< $B000); **zero**
    references into the unmapped `$D000` XLC region.
  - *Dynamic (`tests/sim/swiftaux_copydown_test.py`, py65)*: MAIN loaded at
    $2000, the overlay loaded at $B000 (simulating the copy-down),
    `asc(pool-slot-0)` JSR'd → returns `SE_OK`, pushes `T_INT 104` ('h',
    first byte of "hello, world"). Runs correctly against MAIN-resident
    ZP/stack/data with all aux switches conceptually off.

  Why single-link overlays beat the doc's original "feed MAIN's `.lbl` to a
  second ld65 link": ld65 V2.18 can't ingest a `.lbl` as input symbols, and
  overlays give the same property (each body runs at STAGING) for free in
  one link. Co-location requirement confirmed: `xlc_asc_dispatch` JSRs the
  worker `xlc_asc`, so both must share one overlay segment (they do).
  Slices 1–2 proceed: real `swiftaux-system.cfg`, boot-launcher aux arm
  (chunked staging into the aux park), `aux_xlc_call` trampoline, and the
  ~24-dispatcher port behind this proven mechanism.
- **Q-A2 - Aux park base + staging buffer location/size.** Where in
  aux `$0200-$BFFF` to park the image, and where the fixed main-RAM
  staging hole goes (must not collide with anything live during a
  builtin call). Needs a look at the MAIN BSS map.
- **Q-A3 - Disk geometry.** → **SETTLED 2026-05-31 (user): two 140 KB
  images, one extras binary each** (`make disk` = launcher + lite + SWIFTSAT;
  `make disk-aux` = launcher + lite + SWIFTAUX). A single unified disk carrying
  all binaries is explicitly not pursued. Legacy SWIFTIIX retirement
  already done (commit 0c1950c).
  **SUPERSEDED 2026-06-07 - FIVE-disk set:** once the II+ image filled to 0
  bytes free, the layout split further to **one interpreter per disk** (launcher
  + lite OR launcher + extras, never both): `swiftii-iip-lite` / `-iip-sat` /
  `-iie-lite` / `-iie-aux` + the data disk (`make disks`). The launcher chains
  whichever single binary is present (bidirectional lite↔extras fallback). See
  ROADMAP Phase 14 + docs/contributing/BUILDING.md.
- **Q-A4 - Interim safety.** Ship the routing fix (aux-only → lite)
  *now* as a standalone safety commit, independent of the mechanism
  work? (Recommended - closes the current broken-extras-on-aux gap
  immediately.)

---

# REPL blinking cursor via the XLC trampoline (SWIFTSAT)

`call_xlc_dispatch` also drives one path that is **not** a VM builtin: the
SWIFTSAT REPL's blinking cursor. The 40-column REPL hand-rolls a blinking cursor
(`read_key_blink` + `platform_cursor_cell`, mirroring the editor). On
`SWIFTIIP`/`SWIFTIIE`/`SWIFTAUX` the ~180 B body sits in MAIN; on `SWIFTSAT`
MAIN is full, so the body lives in the XLC overlay.

The wrinkle: `read_key_blink` lives in bank 1, so `platform_read_line` (which
calls it) must run with bank 1 selected. `platform_read_line` has two callers:

- a program's `readLine()` - already in bank 1 (via `xlc_call_builtin_dispatch`),
  so it reaches the bank-1 cursor for free;
- the REPL prompt (`repl.c`) - runs in bank 0.

So the REPL prompt routes its line read through the trampoline: `repl_read_line`
(MAIN) stashes the buffer pointer + length in MAIN-BSS globals, then calls
`call_xlc_dispatch(XLC_OP_REPL_READLINE)` - a new **synthetic** dispatch id
(0x26, JMP-table slot 25, the next free slot after `text80`; it reuses the
`readFile` value, which is WITH_SWB-only and never reaches the SWIFTSAT table).
The bank-1 dispatcher `xlc_repl_readline_dispatch` runs `platform_read_line` in
bank 1 and returns the 0..255 line length through the dispatch's 1-byte A
register (no result global). `repl_read_line` is a plain alias for
`platform_read_line` on every non-SWIFTSAT build.

Why bank 1 is safe for the whole line read: video RAM ($0400) and the keyboard
register ($C000) are not shadowed by the LC bank, cc65's `cgetc`/`kb_ready`
poll $C000 directly (no ROM), and the 40-col echo (cc65 `cputc`) writes the page
directly. The 80-col echo (`cout_char` → `JSR $FDED`) works because on
Saturn-slot-0 *Saturn is the LC*, so `cout_char`'s `bit $C082`/`bit $C080`
ROM-wrap toggles Saturn's own switches and $FDED reads real ROM (verified by the
`videx` acceptance config typing at the 80-col prompt). Slot-N + Videx is the
same pre-existing limit that already governs SWIFTSAT 80-col XLC `print`.

Blink cadence is unified across the REPL, the editor, and the launcher
file-selector: all three toggle on the shared `0x1FFF` counter **and** spend one
keyboard-probe *call* per idle pass (REPL `kb_ready`, editor `kbhit`, launcher
`a_kbd`), so they blink at the same rate. A bare inline $C000 poll has no call,
so it iterates far faster and blinks visibly quicker - hence the deliberate
`kb_ready` call. `kb_ready` is a local probe (not cc65 `kbhit`) so it sits in
the XLC region on SWIFTSAT rather than pulling conio into the full MAIN.

The cursor's ~212 B is in the XLC overlay, so `SWIFTSAT` MAIN keeps 3 B
headroom; Runner/Compiler are byte-identical (the cursor is gated to the REPL
interpreters, WITH_SWB excluded).
