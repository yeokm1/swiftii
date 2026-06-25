# 013 - 80-column text (//e firmware + II+ Videx Videoterm)

This doc covers both 80-column paths: //e firmware 80-column text and II+
Videx Videoterm support. Track B was removed during size work and later
revived; see the track-B addendum for the current constraints.

## Problem

Phase 12 wants 80-column text on the Apple //e (built-in 80-col card),
on the //e disk (`SWIFTIIE` lite + `SWIFTAUX` extras).

The mode switch itself is cheap. The **per-character output path is the
cost**, and it is unavoidably MAIN-resident and hot. Two constraints
shape the whole design:

- Behaviour that differs per machine is keyed off **which disk built
  the binary** (a build flag), not a runtime machine-ID probe - the
  `$FBB3`/`$FBC0` model byte is untrustworthy (emulator presets lie),
  per the Phase 11 slice-4 lesson. Detecting a *card's presence* at
  runtime is still fine; that is a capability probe, not a model probe.
- The `WITH_80COL` flag already exists (`WITH_80COL ?= 1`) but **no
  source consumes it yet** - it is a reserved no-op this phase makes
  real (passed to the `WITH_IIE` binaries only).

### Why the output path is the cost, not `text80()`

SwiftII renders through cc65 `apple2` conio (`cputc`), which writes the
40-column **main** text page only. (Phase 11 slice 4 already found that
`cputc` folds `$60-$7F` to uppercase and switched to writing screen
codes directly via `emit_native_high` - so the emit path is already
custom and fragile.) In 80-column mode the hardware interleaves two
pages: **main** `$0400-$07FF` holds even columns (0, 2, 4, …) and
**aux** `$0400-$07FF` holds odd columns (1, 3, 5, …), selected by
`80STORE` + `PAGE2`. Driving 80 columns therefore means either:

1. **Route output through the //e 80-col firmware COUT** (`JSR $C300`
   hooks CSW), letting the firmware do the column interleave. Cheap in
   our code, but it is a *different output path* and gives up the
   direct-screen-code control slice 4 needed; and
2. **Hand-roll the main/aux interleave** in `screen.c` (track an
   80-wide cursor, pick main-vs-aux page per column via `80STORE`/
   `PAGE2`/`RAMWRT`). Full control, but a meaty hot-path rewrite in
   MAIN.

Either way the emit change lives in MAIN (it cannot move to a
copy-down overlay - same wall the Phase 11 platform parser table hit),
on the 40-col primary path, with real regression risk. This is the
make-or-break of the phase and must be settled by a gate slice before
anything else is built - exactly the shape of Phase 11 slice 0.

## Proposal

**Phase 12 = 80-column text on the //e disk**, behind one `screen.c`
width abstraction:

- The **extras binary is the primary target** (it owns the `text80()`
  builtin via its copy-down mechanism), and the **lite binary is a
  conditional stretch** (80-col only if the gate slice shows the
  output-path MAIN cost fits lite's headroom AND a //e card is detected
  at runtime).

1. **`SWIFTAUX.SYSTEM` (//e, primary).** `text80()` / `text()` mode
   switches ship as **copy-down overlay builtins** (dispatcher in
   `builtins_xlc.c`, parser branch gated `… || WITH_SWIFTAUX`, body
   parked in aux and copied down per call). The mainstream target -
   Phase 11 exists in part to unblock it.
2. **`SWIFTIIE.SYSTEM` (//e lite) - conditional stretch.** Only if the
   gate slice measures the output-path MAIN cost fitting SWIFTIIE's
   headroom (~276 B at Phase 11 close) **and** a //e 80-col card is
   detected at runtime. Lite has no XLC/copy-down, so the `text80()` /
   `text()` ids run inline in the VM path; the lite story is "detect
   card at init, run the REPL in 80 columns if present and the code
   fits." If it doesn't fit, SWIFTIIE stays 40-col - SWIFTAUX still
   ships.

(The II+ disk - `SWIFTIIP` lite + `SWIFTSAT` extras - has no 80-col
path and stays 40-column.)

### Why a runtime card probe on lite does *not* violate the slice-4 lesson

Slice 4 abandoned the `$FBB3`/`$FBC0` *model* byte for choosing MAIN
*behaviour*, because the model byte is untrustworthy. Detecting a
*card's presence* is a different, legitimate runtime capability probe -
the boot launcher already does it (ProDOS MACHID `$BF98` bit 4, plus the
80-col firmware presence). 80-col behaviour on lite //e keys off "is
there a card to talk to," not "what model does $FBB3 claim." The
build-flag gate (WITH_IIE / the //e disk) still decides whether the
80-col *code* is present at all; the runtime probe only decides whether
to *use* it.

### The basic-vs-extended 80-col card cohort

This is why lite //e matters at all. The //e 80-col cards split:

- **Extended 80-col card (64K aux):** routes to `SWIFTAUX` (MACHID bit
  4 set, full aux). Has both the aux text page *and* the 64K aux the
  copy-down park needs ($2000-$77FF). Full 80-col via SWIFTAUX.
- **Basic 80-col card (1K aux - text page only):** MACHID bit 4 is also
  set, so the boot launcher currently false-positives it toward extras
  (boot_launcher_asm.s notes this) - but SWIFTAUX's park can't fit in 1K of
  aux. The *correct* binary for this cohort is **lite //e**, and the
  aux text page **is** present (that 1K *is* the odd-column page). So if
  the lite output-path change fits, this cohort is precisely who gets
  80-col from SWIFTIIE.

Boot launcher routing for the basic-80-col case (currently a
false-positive toward an extras binary whose park won't fit) is a
sub-item the gate slice should confirm/repair.

## Memory: no conflict

The 80-col aux text page is **aux `$0400-$07FF`**. SWIFTAUX's copy-down
park is **aux `$2000-$77FF`** (MEMORY_MAP.md, "SWIFTAUX aux copy-down
layout"). They do not overlap. Both paths toggle `RAMWRT`/`RAMRD`/
`INTCXROM`, but never concurrently - one builtin runs at a time, and
the trampoline restores switches around each AUXMOVE. A `text80()`
overlay that writes the aux text page must leave the soft switches as
the trampoline expects on return; a design note, not a blocker.

## Detection strategy

The **build gate selects whether the driver is compiled** (the //e
disk); a **runtime card-presence probe** decides whether to use it. No
runtime `$FBB3`/`$FBC0` *model* probe - the slice-4 lesson is never to
key MAIN behaviour off the untrustworthy model byte, but a
*card-presence* probe is a legitimate capability check (the boot
launcher already does one).

- **//e disk (`WITH_IIE`):** compile the 80-col driver. Runtime probe =
  MACHID `$BF98` bit 4 + //e 80-col firmware presence (reused from the
  boot launcher). Gates whether lite //e enters 80-col and whether
  `text80()` on SWIFTAUX acts vs. no-ops.
- On the wrong hardware `text80()` is a no-op.

## Slice plan

Track A (//e) shares one `screen.c` width abstraction; the `apple2`
build target stays (not `apple2enh` - preserves //+ compat per
CONSTRAINTS.md).

- **Slice 0A (gate, //e).** Spike the //e 80-col output path. Decide
  **firmware-COUT vs hand-rolled aux interleave**. Measure MAIN cost vs
  SWIFTAUX headroom and (for the stretch) SWIFTIIE headroom. Prove the
  40-col path stays byte-identical when off. GO/NO-GO before building
  the rest - Phase 11 slice 0 shape.
- **Slice 1A.** `text80()` / `text()` as SWIFTAUX copy-down overlay
  builtins (mode switch + clear); `htab` honours 1–80; `screen.c`
  tracks current width (40 vs 80) and selects the emit path.
- **Slice 2A (conditional on 0A).** SWIFTIIE lite enters 80-col at init
  when a //e card is detected; confirm/repair boot-launcher routing for the
  basic-80-col cohort.

## Resolved questions

- **Q1 - //e output path. RESOLVED 2026-05-31 (slice 0A): firmware-COUT.**
  Measured cost is **+93 B MAIN** per //e binary, fitting both SWIFTAUX
  (367→274 B headroom) and the SWIFTIIE stretch (276→183 B), with the
  40-col path byte-identical when compiled out. The doc's worry that
  firmware-COUT "gives up the direct-screen-code control slice 4 needed"
  was based on a false premise: slice 4 only needed `emit_native_high`
  because cc65 `cputc` folds $60–$7F to uppercase - the //e firmware has
  no such bug and renders lowercase correctly, so firmware-COUT is both
  cheaper *and* more correct than the hand-rolled interleave. The
  hand-rolled aux-interleave alternative is shelved unless a later slice
  surfaces a firmware-COUT reliability problem under ProDOS.
- **Q3 - does the lite binary get 80-col at all?** Strictly contingent
  on the slice-0 measurement: SWIFTIIE against ~276 B. Default if it
  doesn't fit: extras-only on the disk, lite stays 40-col. (It fit -
  SWIFTIIE ships 80-col.)
- **Q4 - `htab`/`vtab`/`scrn` width semantics.** In 80-col, `htab`
  range becomes 1–80; confirmed isolated during slice 1A (the firmware
  cursor column is OURCH `$057B`). `vtab` (rows 1–24, unchanged range)
  was wired for 80-col in Phase 16: poke the monitor row CV `$25` - but
  the **//e** firmware caches the line base, so it also re-runs `VTAB
  $FC22` (BASCALC); the **Videx** re-derives per char and must not. Both
  confirmed on real hardware (II+ Videx + //e aux).
- **Q5 - shared abstraction.** `screen.c` tracks width and the 80-col
  emit path is gated `WITH_80COL` (= `WITH_IIE`). Confirm the
  abstraction is thin enough that it doesn't bloat MAIN when off
  (byte-identical with `WITH_80COL=0`).

## Phase dependency

Phase 8 (HW detection + boot launcher + build matrix), Phase 10 (text
APIs + the copy-down/XLC builtin mechanism), and **Phase 11**
(`SWIFTAUX` copy-down - `text80()` is an XLC builtin like every other
platform API and cannot run on a //e until that trampoline exists).

---

## Track B addendum - II+ Videx Videoterm (Phase 12, revived Phase 16)

### The shared insight

Track A's resolved Q1 (firmware-COUT) generalises: **any** slot card whose
firmware hooks `CSW` and renders lowercase natively can drive 80 columns
through `COUT` (`$FDED`) with the same thin `screen.c` width abstraction. The
II+ Videx Videoterm (slot 3) is exactly that card. So track B reuses track A's
per-character path verbatim - `emit()` sends a CR for either newline byte and
the raw high-bit byte otherwise - and differs only in three machine-specific
points, selected in `screen.c` by `WITH_IIE` (track A) vs `!WITH_IIE` (track
B), both under the single `WITH_80COL` flag:

| Concern | Track A (//e) | Track B (Videx) |
|---|---|---|
| Activation | `JSR $C300` (immediate) | `CSW=$C300` (PR#3; firmware cold-inits on the **first** COUT) |
| Card probe | ProDOS MACHID `$BF98` bit 4 | slot-3 Pascal-1.1 sig `$C305==$38 && $C307==$18` |
| `COUT` call | bare `jsr $FDED` | ROM-wrapped `bit $C082` / `jsr $FDED` / `bit $C080` |

The ROM wrap is the one real subtlety: on the II+, SwiftII (and ProDOS's MLI)
map language-card RAM bank 2 over `$D000–$FFFF`, so `$FDED` is RAM, not ROM -
bank motherboard ROM in for the call, restore bank 2 after. Bank 2 is what both
the interpreter's LC code and ProDOS map there, so one wrap is correct for every
II+ build. The deferred (CSW) activation means the firmware's `BIT $FFCB`
cold-init dispatch fires inside that ROM-wrapped COUT, where `$FFCB` reads real
ROM - the detail that made `JSR $C300` work on iron but look broken under
izapple2's divergent ROM.

### Why real hardware, not the emulator, is the source of truth

The original track B was removed (2026-06-06) because it is **hard to verify in
emulators**, not because it was wrong: Mariani has no Videoterm; izapple2's
bundled `Videx Videoterm ROM 2.4.bin` diverges from real cards (its `$FFCB`
differs, and a hand-rolled `JSR $C800` init reverse-engineered from it failed on
iron - the real ROM's init is behind the `$C300` `BIT $FFCB` dispatch). The
shipped activation (PR#3 `CSW=$C300`), the relaxed probe (`$C305/$C307`, **not**
the rev-varying `$C30B`), and the Ctrl-L re-entry clear were all confirmed on a
real II+ + Videoterm in Phase 12. The current REPL smoke target is
`make run-iz-videx`, which boots `SWIFTSAT`, so it is a Saturn + Videx profile
and remains an output-routing smoke test only.

### Phase 16 scope - which II+ binaries carry it

Revived across every II+ binary that can hold it, each with its natural opt-in:

- **`SWIFTSAT`** (II+ Saturn extras REPL) - the proven primary. `text80()` /
  `text()` are XLC builtins (slot 24 / slot 17); default 40-col, opt-in.
  `+195 B` MAIN (124 B headroom remains).
- **Family B `RUNNER`** (flat II+ and Saturn-paged) - program opt-in via the
  same `text80()` / `text()` ids, dispatched through `builtins_xlc.c` (the
  `vm.c` `WITH_SWB` catch-all). The flat Runner trims its heap 3840→3584 to fund
  the Videx code's cc65 `-Cl` static-local BSS; the Saturn Runner's windowed
  bytecode path leaves enough BSS to fit at its existing heap.
- **`SWIFTIIP`** (II+ lite REPL) - **stays 40-col.** A silent auto-jump to
  80-col was tried (it fit) but unwanted (user direction, 2026-06-14), and lite
  cannot host the `text80()`/`text()` opt-in: measured **+122 B** (text80 only) /
  **+202 B** (with text) over the 64K *code* ceiling - a MAIN/LC overflow, not a
  buffer trade, so unbuyable without a code-size pass (`input_translate`, the one
  big dead-code lever, is *live* on the II+). So lite has no Videx path at all
  (byte-identical to main); for 80-col in a II+ REPL, use SWIFTSAT, which
  means Saturn + Videx.
- **`SWIFTSAT`** banner prints `Videx 80-col card: type text80()` when a card is
  detected (`platform_videx_present()`), since it does not auto-enter. The boot
  launcher's DEBUG screen shows the raw `$C305`/`$C307` probe bytes.
- **Family B `COMPILER`** - no binary change: it already parses `text80()`
  (the `WITH_SWB` row in `builtin_calls.c`) and stays a 40-col tool.

`htab` 1..80 works via the firmware cursor column OURCH `$057B` (same address as
the //e). `vtab` 1..24 also works in 80-col (Phase 16) via the monitor row CV
`$25` - the Videx honours the bare poke (its 6845 firmware re-derives the
address per char), whereas the //e needs an extra `VTAB $FC22` because its
firmware caches the line base (see the 2026-06-14 LESSONS entry). The `text()` 40-col revert needs **two** steps, both confirmed on real
hardware (2026-06-14): AN0 OFF (`$C058`) to hand the display back via the Soft
Video Switch - PR#0 (`CSW=$FDF0`) alone does NOT, it only stops *routing* to the
card - then the CSW reset. `text80()` re-entry sets AN0 ON (`$C059`) since the
firmware cold-init only does so the first time. (`screen.c platform_text40` /
`platform_text80`.)

### Why II+ Videx 80-col is REPL-only (the RAM wall)

Reviving track B 80-col on the II+ *launcher*, *editor*, and the *lite REPL
toggle* was attempted (2026-06-14) and **does not fit** - the II+ binaries sit
at the RAM ceiling where the //e equivalents have kilobytes free, because the
II+ build carries the pre-IIe inverse-video rendering + the `input_translate`
case-folding machinery that the //e (`LITE_IIE`/`WITH_IIE`) build drops. Measured
overflows: `SWIFTIIP` lite `text80()`/`text()` builtins **+202 B** over MAIN
(8 B free); the II+ launcher's menu/browser/Help 80-col **+1187 B** over BSS;
adding the in-editor 80-col frame **+2332 B**. So II+ Videx 80-col ships in the
REPLs only - `SWIFTSAT` (opt-in `text80()`, therefore Saturn + Videx) and the
Family B `RUNNER` (program output, including the flat II+ tier). The
launcher/editor stay 40-col on the II+ disks (a Videoterm user edits in 40-col,
then previews/runs in 80-col). Reopening any of these needs a dedicated II+
size-reduction pass first.

### Program-output convention: don't `text()` at the end

An 80-col program run by the Runner should **not** call `text()` to "clean up"
at the end. `text()` does `clrscr` + flips to 40-col Apple video, which wipes
the program's own 80-col output before the Runner's "Press any key" pause. The
right model: the program leaves 80-col ON, so its result stays up and the
Runner's pause prints on the Videoterm too (the pause goes through
`platform_putc` → `emit`, which honours `s_width`); the **Runner** then reverts
to 40-col on exit (before chaining to the 40-col launcher) - the //e Runner's
`$C00C` reset suffices, the II+ Videx Runner adds AN0 OFF + `CSW→COUT1`
(`platform_text40()`, `runner_main.c`, gated `WITH_80COL && !WITH_IIE`). See
`progdisk/fbsamples/xwide.swift` for a worked 80-col game, and LESSONS 2026-06-14.

### Status

**Real-HW verified 2026-06-14** on a II+ + Saturn 128K + Videx Videoterm:
SWIFTSAT `text80()`/`text()`/`htab`, Family B Runner program output
(`xwide.swift`), the `text()` AN0 revert, output persistence, and the Compiler
`.swb` `$48` fix all confirmed end-to-end. `make run-iz-videx` remains a
Saturn + Videx izapple2 output-routing smoke test only (its bundled Videoterm
ROM diverges).
