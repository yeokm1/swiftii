# 010 - Hardware capability detection and build matrix (Phase 8)

The capability-detection rationale still stands, but the original Phase 8
two-binary matrix is historical. The current build set is the per-machine
split (`SWIFTIIP`/`SWIFTIIE` lite, `SWIFTSAT`/`SWIFTAUX` extras) plus the
Family B Compiler/Runner. See [PROJECT_LAYOUT.md](../PROJECT_LAYOUT.md) and
[FEATURES.md](../../using/FEATURES.md).

## Problem

The lite-binary `SWIFTIIL.SYSTEM` is at **40,677 B / 27 B headroom**
against the ProDOS-SYS 40,704 B load ceiling (Phase 7 c1–c8a +
peek/poke). Every Tier 2 feature still on the menu - closures,
`Dictionary`, REPL history, 80-column text, full FP, HGR - adds
several KB on its own. There is no room in the lite binary for any of
them; Phase 7 already pulled `home`/`htab`/`vtab` builtins for this
exact reason (see [LESSONS.md](../LESSONS.md) 2026-05-24).

The decision recorded in [ROADMAP.md section Phase 8](../ROADMAP.md) is to
split the interpreter into **two binaries** for v1:

- `SWIFTIIL.SYSTEM`  - lite, runs on every Apple II Plus / IIe / IIc
- `SWIFTIIX.SYSTEM` - extras, requires ≥128 KB total RAM (II+ with
  Saturn, IIe with 64K aux, etc.)

Phase 8 ships the infrastructure: detection so each binary knows
what's available, a build matrix so per-feature code lives in the
right binary, and a boot launcher so the disk image transparently routes
to the right one. Phase 8 does **not** ship a generic runtime
allocator over the extra RAM - that question is deferred to whichever
Phase 9+ feature first needs it, since each feature has its own
opinion on where extra RAM is useful (80-col uses the aux text page,
HGR uses the aux HGR page, closure upvalues TBD, etc.).

Today there is no:
- runtime probe for Saturn 128K or IIe aux RAM,
- `WITH_EXTRAS` or per-feature build flag,
- second cc65 build target,
- boot launcher to pick a binary at launch - `build_po.sh` (pre-Phase 8)
  put the lite interpreter directly on the disk under its original
  name `SWIFTII.SYSTEM` and ProDOS auto-launched it. Post-commit 4:
  the launcher claims that name and the lite interpreter is installed as
  `SWIFTIIL.SYSTEM` (see LESSONS.md 2026-05-25 - ProDOS 2.4.3 only
  auto-launches `*.SYSTEM`-suffixed files).

## Proposal

Ship Phase 8 as **four commits**, ordered low-risk-first so each
commit closes with a green tree (`make ci` builds and size-checks
both binaries):

```
commit 1: design doc (this file).               no code; alignment.
commit 2: capability struct + host stub.        platform_capabilities_t
                                                in osdetect.h, lite
                                                interpreter populates
                                                machine_type from $FBB3,
                                                host stub for tests.
                                                No probes for Saturn /
                                                aux / 80-col in lite -
                                                preserves the 27 B
                                                lite-binary headroom.
commit 3: build matrix.                         WITH_EXTRAS umbrella
                                                flag, make apple2-extras
                                                target, second output
                                                SWIFTIIX.SYSTEM (byte-
                                                identical to lite apart
                                                from is_extras_build=1
                                                inside osdetect_init).
                                                make ci builds + size-
                                                checks both.
commit 4: boot launcher.                            tools/apple2/boot_launcher/
                                                produces SWIFTII (the
                                                launcher, ~512 B); build_po.sh
                                                bundles launcher + lite +
                                                extras on swiftii.po.
                                                Launcher probes hardware,
                                                picks one of the
                                                interpreter SYS files,
                                                chains via ProDOS MLI.
```

After commit 4 the disk image boots into the boot launcher, which decides
between `SWIFTIIL.SYSTEM` and `SWIFTIIX.SYSTEM` based on detected
capabilities, then chains. (HGR build variants were once contemplated
as a third axis here but never shipped; high-resolution graphics is a
backlog item, see [ROADMAP-MAYBE.md](../ROADMAP-MAYBE.md).)

## Detailed design

### Commit 2 - capability struct

A single source of truth at `src/platform/apple2/osdetect.c`. The
7-byte struct in `osdetect.h`:

```c
typedef struct {
  unsigned char machine_type;       /* conservative pre-IIe default; not runtime-probed */
  unsigned char saturn_slot;        /* 0 = none, else slot 1-7 */
  unsigned char has_aux_ram;        /* IIe 64K aux card present */
  unsigned char has_80col;          /* IIe 80-column firmware present */
  uint16_t total_extra_ram_bytes;   /* sum of saturn + aux */
  unsigned char is_extras_build;    /* set at compile time from WITH_EXTRAS */
} platform_capabilities_t;

extern platform_capabilities_t platform_caps;

void osdetect_init(void);
```

The interpreter does not probe `$FBB3` at runtime (it runs with the
language card banked in, so that read returns RAM, not the ROM ID byte);
`machine_type` keeps a conservative pre-IIe default and the pre-IIe glyph
substitution is driven by the `WITH_IIE` build flag. The Saturn / aux RAM /
80-col fields stay zero in the lite binary - those probes live in the boot
launcher so they never enter the lite-binary footprint. The extras
interpreter could re-probe in
a future commit if it grows a feature that needs the data; today the
extras `osdetect_init` only sets `is_extras_build = 1`.

Expected cost: +7 B (40,677 → 40,684 B), 20 B remaining lite-binary
headroom.

Host stub at `src/platform/host/osdetect.c` defaults to a IIe with
no extra RAM. Three unit tests cover defaults, init behavior, and
struct size bound.

### Commit 3 - build matrix

One source tree, two cc65 invocations, two outputs.

`WITH_EXTRAS` is implicit in the `apple2-extras` recipe (the recipe
always passes `-DWITH_EXTRAS`). Per-feature flags default ON and can
be flipped off for development:

```
WITH_CLOSURES ?= 1
WITH_DICT     ?= 1
WITH_HISTORY  ?= 1
WITH_80COL    ?= 1
```

Each flag turns into a `-DWITH_FOO=1` on the cc65 command line; C
code opts in via `#ifdef WITH_FOO`. Until Phase 9 the gates do
nothing - the macros propagate but no source consumes them.

Two output targets:

```
apple2          → build/apple2/SWIFTIIL.SYSTEM         (lite)
apple2-extras   → build/apple2/extras/SWIFTIIX.SYSTEM (extras)
apple2-all      → both, with apple2-extras order-only depending on
                  apple2 to serialize cl65 (cc65 drops .o next to
                  source; the two rules would otherwise race on
                  `find src -name '*.o' -delete` under make -jN).
```

Both share `src/platform/apple2/swiftii-system.cfg` for now. When
budgets diverge in Phase 9+ (extras likely needs different
LC/CODE/BSS apportionment) we split into `swiftii-extras.cfg`.

`make size` reports both binaries against per-binary budgets and
fails on overrun:

```
LITE_SYS_BUDGET   := 40704
EXTRAS_SYS_BUDGET := 40704
```

`make ci` ends with `apple2-all size`. The extras binary is
byte-identical to lite apart from the `is_extras_build = 1` store
inside `osdetect_init` (~5 B). Expected sizes: lite 40,684 B (20 B
headroom), extras 40,689 B (15 B headroom).

### Commit 4 - boot launcher

`tools/apple2/boot_launcher/` produces a standalone ProDOS-launched program:

- `boot_launcher.s` - ca65 source. Loads at `$2000` like any SYS file.
  Runs hardware probes (machine type via `$FBB3`; Saturn slot scan
  for `$Cn00..$Cn07` Saturn ID bytes on slots 7..1). Picks one of
  `SWIFTIIL.SYSTEM` / `SWIFTIIX.SYSTEM`. Issues ProDOS MLI
  `OPEN`/`READ`/`CLOSE` against the chosen filename, then `JMP $2000`
  (overwriting itself with the chosen interpreter). Standard
  SYS-file selector pattern.
- `boot_launcher.cfg` - minimal ld65 config: ZP `$80-$8F`, MAIN `$2000`
  up to `$4000`. No LC, no significant BSS. Stays well under 512 B.
- `Makefile`: new `make boot-launcher` produces `build/boot_launcher/SWIFTII`
  (the bare filename, no `.SYSTEM` suffix - ProDOS launches the
  first SYS-type file after `PRODOS` and we want the launcher to be that
  file).

The launcher only differentiates lite vs extras; the dispatch picks
lite/extras unconditionally based on hardware. (An `#hgr` pragma scan
that would route to dedicated HGR binaries was sketched here but never
built - high-resolution graphics is a backlog item, see
[ROADMAP-MAYBE.md](../ROADMAP-MAYBE.md).)

Dispatch is by disk-selected build flag, not a runtime `$FBB3` probe (the
machine-ID byte is hidden by LC banking and lies under emulator presets; see
design doc 013). Each disk carries one interpreter and the launcher uses it.
The pre-IIe cohort, where it matters for glyph rendering, is `$FBB3 != $06`:
the original ][ is `$38` and the II+ is `$EA` (Tech Note Misc #7); the IIe
family is `$06`.

The capability struct is **not** round-tripped from launcher to
interpreter. The lite interpreter doesn't care about Saturn / aux /
80-col (it has no features that use them). The extras interpreter
re-probes if and when a future feature needs the data.

**`build_po.sh` rework** - previously put the lite interpreter directly
on the .po under the name `SWIFTII.SYSTEM`. After commit 4:
1. Add `SWIFTII.SYSTEM` (launcher, type SYS) - first `*.SYSTEM` file in
   directory order so ProDOS auto-launches it. The build artifact is
   `build/boot_launcher/SWIFTII` (no suffix on the filesystem); the
   `.SYSTEM` suffix is added when `ac -p` writes it to the .po.
2. Add the lite interpreter (`SWIFTIIL.SYSTEM` at the time; Phase 11
   split it into `SWIFTIIP.SYSTEM` / `SWIFTIIE.SYSTEM`).
3. Add the extras interpreter (`SWIFTIIX.SYSTEM` at the time; Phase 11
   split it into `SWIFTSAT.SYSTEM` / `SWIFTAUX.SYSTEM`).

The lite interpreter's filename moved from `SWIFTII.SYSTEM` to
`SWIFTIIL.SYSTEM` at the same time, freeing the `SWIFTII.SYSTEM`
name for the launcher - ProDOS 2.4.3's Bitsy Boot filters specifically
for `*.SYSTEM`-suffixed files, so a launcher named just `SWIFTII` would
be skipped and ProDOS would launch the next `*.SYSTEM` file in
directory order (which would be the lite interpreter, bypassing the
selector entirely). See LESSONS.md 2026-05-25.

### MEMORY_MAP.md edits

Commits 2 and 3 each add a short subsection under the
"Multi-binary layout (Phase 8+)" entry documenting the capability
struct's BSS/DATA cost and the build-matrix output paths. Commit 4
adds nothing new to the memory map (the launcher is a separate binary;
the interpreter binaries are unchanged).

### ROADMAP.md edits

Each commit ticks the corresponding bullets in Phase 8. Commit 4
also flips the "Runtime use of extra RAM" section to **deferred**
with a pointer to this doc - the generic heap-growth /
source-buffer-growth / bytecode-arena-growth bullets move to a
deferred bucket, to be picked up by whichever Phase 9+ feature first
demands them.

---

## Alternatives considered

**One big binary with everything gated at runtime**. Rejected by the
basic problem statement - we are 27 B from the ceiling. There is no
"runtime gate" that gives us room for Tier 2 features in the lite
binary; the code must be physically absent.

**Three or more binaries from day one** (e.g. split closures and
dictionary into separate builds). Rejected: combinatorial Makefile
explosion, no demand yet, the 64K-machine cohort doesn't get any of
them either way. Two binaries is the minimum viable split; if a
third shape emerges later we add it then.

**Boot launcher embedded inside the interpreter**. Rejected: the launcher
needs to run *before* deciding which interpreter to load, so it
can't live inside either of them. A standalone ~512 B selector is
the standard ProDOS pattern.

**Skip the boot launcher; ship two .po images** (one per binary).
Rejected: the project promise is a single disk image that "just
works." Forcing the user to guess at their own hardware tier
defeats the point of the detection layer.

**Bundle a generic two-arena allocator over Saturn or aux RAM into
Phase 8**. Rejected. Saturn 128K is a language-card *replacement*
card mapping into `$D000-$FFFF` - directly on top of SwiftII's LC
code (compiler, REPL driver, file-runner). Banking Saturn in evicts
our own code; banking it back out for every code call from a heap
operation is doable in principle but adds cross-bank dispatch
overhead to every heap touch and a new failure mode (forget the
bank-out, crash inside compiler code). Saturn is emulatable -
Mariani (AppleWin fork) supports Saturn 128K as a slot card, so a
test config is reachable; the rejection is on complexity grounds,
not testability. The Phase 9+ features that would benefit from
extra RAM mostly want fixed regions (text page, HGR page), not a
generic allocator. Designing one now would tie up Phase 8 without a
confirmed consumer. Likely first real consumer is **80-column text
mode** (Phase 12), which uses the fixed `$0400-$07FF` aux text page
and doesn't need an allocator at all.

---

## Cost

- **Memory cost**:
  - Lite binary: +7 B for the struct (commit 2); 0 B from commit 3
    or 4. Total: 40,677 → 40,684 B (20 B headroom remains).
  - Extras binary: +5 B for the `is_extras_build = 1` assignment
    (commit 3). Total: 40,684 → 40,689 B (15 B headroom remains).
  - Boot launcher: separate binary, ~512 B. No impact on the interpreter
    binaries.
- **Performance cost**:
  - `osdetect_init` adds ~1-2 ms to interpreter startup. Cold-start
    budget (4 s for hello world) absorbs this trivially.
  - Boot launcher adds ProDOS MLI open/read/close + JMP - under 0.5 s
    on a real Apple II.
  - No per-operation overhead since the runtime allocator is
    unchanged.
- **Code complexity cost**:
  - Two cc65 invocations from one Makefile - modest, standard.
  - Boot launcher is a new code surface (~150 lines of asm) plus a
    new .cfg, but small, isolated, rarely touched.
  - heap.c stays the simple single-arena bump allocator it is today.
- **Capability cost**: no runtime extra-RAM use in Phase 8. User
  programs still cap at the lite binary's heap/source/bytecode
  sizes. This is the real tradeoff; the call is to defer until a
  real consumer demands the work.
- **Schedule cost**: commits 1–3 are each session-sized. Commit 4
  estimated 1-2 sessions (asm + MLI plumbing + emulator
  verification of the chained launch).

---

## Migration

- **Existing tests**: all current host tests pass with `WITH_EXTRAS`
  unset (default). New tests gated by `#ifdef WITH_EXTRAS` arrive
  only when Phase 9+ features land.
- **Existing on-disk binaries**: none - the project is pre-1.0 and
  the .po image is rebuilt every commit.
- **Lite-binary filename change**: `SWIFTII.SYSTEM` → `SWIFTIIL.SYSTEM`
  (commit 4). Any tooling outside the repo that hardcodes the old
  name needs an update. The boot launcher takes the freed `SWIFTII.SYSTEM`
  name and is what ProDOS auto-launches.

---

## Open questions

1. **Should the boot launcher probe Saturn at all** in Phase 8, given
   nothing downstream uses the result? - **Resolved 2026-05-25
   (commit 4)**: yes, but the signature-scan plan above ("scan slot
   ROM at $C400-$C7FF") is wrong. Saturn 128K has *no slot ROM* -
   reads from $Cn00..$CnFF return floating bus, confirmed against
   the AppleWin Saturn implementation. Replaced with a behavioural
   probe: force main LC to bank-2 / WRITE PROTECT, then for each
   slot 7..1 do two reads of $C0N3 (LC-convention R+W enable) and
   verify a sentinel write at $D000 took effect. ~50 B of asm; the
   risk of corrupting ProDOS MLI is contained by the WRITE PROTECT
   on main LC (writes drop on no-card slots). The probe will
   false-positive on any other LC-class card in slots 1..7, but the
   correct action for those machines is still "route to extras" so
   the behaviour matches intent.

2. **Should the boot launcher stash the probed capabilities** anywhere
   for the chosen interpreter to read? - **Resolved 2026-05-25
   (commit 4)**: no. The chosen interpreter re-probes only what it
   cares about, when it cares about it. Avoids a shared-memory
   contract between two independently-built binaries.

3. **Boot launcher chaining mechanism**: ProDOS MLI direct
   (OPEN/READ/CLOSE/JMP) vs setting the QUIT pathname. - **Resolved
   2026-05-25 (commit 4)**: MLI direct. Trickier than first
   advertised because MLI READ writes the chained interpreter on
   top of the launcher's own $2000+ code, so the CLOSE call + JMP $2000
   that runs after READ has to live somewhere READ can't reach.
   Solution: a 15-byte bouncer + 10 bytes of READ/CLOSE param
   blocks copied into ZP $90-$A9 before the READ, then JMP into the
   bouncer. The ZP-bouncer cost is +30 B vs a naive in-place
   sequence but keeps the chaining standalone (no dependency on a
   particular QUIT.SYSTEM dispatcher).

4. **`make ci` runtime**: building both binaries roughly doubles
   `make apple2` time (~6 s on a fast Mac). **Acceptable** -
   doubling doesn't change the developer workflow.

---

## Decision

**Accepted** 2026-05-25. **Shipped** 2026-05-25 (all four commits +
boot-launcher Saturn-probe + REPL banner differentiation). Runtime
extra-RAM use deferred to whichever Phase 9+ feature first
demands it, with a fresh design doc at that point.

---

## Resolved: SWIFTIIX split into Saturn-vs-aux variants

Deferred at Phase 8, then **resolved in Phase 11**: the single extras
binary was split into `SWIFTSAT.SYSTEM` (Saturn) and `SWIFTAUX.SYSTEM`
(//e aux). The two storage architectures differ enough to warrant
separate builds - Saturn is bank-switched 16 KB windows at
`$D000-$FFFF` via slot N's `$C0Nx` switches; //e aux is a flat ~48 KB
at `$0200-$BFFF` (RAMRD/RAMWRT) plus a 16 KB aux LC bank (ALTZP). The
boot launcher detects both signals independently and routes to the
matching binary. See design [011](011-extras-lc-in-saturn-aux.md) for
the split's design.
