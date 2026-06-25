# 018 - On-target test harness ("Run all tests" from the launcher)

The on-target harness is emulator-verified via `tools/host/acceptance/`;
see "Automated driver" and "Decision" below.

## Problem

The host suite (`make test` / `make sim`) runs in seconds, but it does
not exercise the *target*: cc65 codegen, the 64 K memory layout, the
real MLI, the Family-B Compiler→Runner→`.swb` path, aux/Saturn paging,
the firmware screen drivers. The only way we catch target-specific
regressions today is to boot an emulator (or real iron), open the
launcher's file browser, navigate into `TESTS/` (or `XTESTS/` /
`FBTESTS/`), pick one `.swift`, run it, read the last line by eye
(`fail 0` = clean), watch it cold-reboot back to the launcher, and then
repeat - once per test file. With ~30 self-checking tests across the
tiers, on-target verification is a slow, manual, error-prone slog. This
is precisely the work blocking the v1.0 real-HW gate (Phase 17), and it
is the step most worth speeding up because it is run repeatedly.

Two things make it slow that are independent:

1. **Navigation.** Every test costs a browse-into-dir, select, run,
   reboot, browse-back cycle. The reboot between programs is structural
   (a run cold-reboots to the launcher; nothing in RAM survives - that
   is why the `LASTRUN` resume note exists on disk at all), so the
   per-test reboot cannot be removed. But the *navigation* around it can.
2. **The flat layout.** The tiers live in four sibling top-level ProDOS
   directories (`TESTS/`, `XTESTS/`, `RTESTS/`, `ERRTESTS/`), so a
   harness that wants "every test applicable to this disk" has to know
   and walk four unrelated roots.

## Proposal

Three changes, the first two shipped together, the third as a follow-on:

1. **Restructure the on-disk tests under one parent.** Collapse the four
   sibling tier directories into a single `TESTS/` tree with one
   subdirectory per tier, so the whole suite is one walkable root:

   ```
   TESTS/
     CORE/      general    - any REPL (lite or extras)      [was TESTS/]
     XTESTS/    extras     - SWIFTSAT / SWIFTAUX REPL        [was XTESTS/]
     FBTESTS/   Family B   - compiler-runner                 [was RTESTS/]
     ERRTESTS/  demos      - deliberately fail, not checked   [was ERRTESTS/]
   ```

   Mirror the move in the source tree (`datadisk/`) and the data-disk
   build (`build_data_po.sh` + the Makefile vars). The general tier needs
   a name now that `TESTS/` is the parent - `CORE/` (shipped as
   `TESTS/CORE/`).

2. **REPL-tier approach - auto-sequencer in the launcher ("you verify").**
   Add a launcher command that runs *every test applicable
   to the booted disk's binaries* back-to-back, with no per-test browsing.
   The human stays the oracle: they read each test's `fail 0` line on
   screen and press one key to advance to the next. The launcher persists
   its place in the queue across the mandatory reboot (a small extension
   of the existing `LASTRUN` note) and shows a "ran N tests" summary at
   the end. This is the cheapest possible win in bytes - critical because
   the interpreters are at the 64 K ceiling and the launcher is where the
   headroom is - and it works uniformly across all four binaries.

3. **Compiler-runner approach - results log (follow-on, auto-verify).** On
   the compiler-runner disks only, have the Runner record a machine-read
   verdict per test to a `TESTLOG` file on the data disk, so the harness
   can report pass/fail itself (no human eyeballing) and leave a
   persistent report. This is Family-B-only by construction: the Runner
   has the file-CRUD path (doc 017) and MLI intact; the Family-A REPLs
   hold the interpreter in the language card, not MLI, and cannot write
   files. Screen-scraping the other tiers (the rejected screen-scrape
   approach) is explicitly **not** in scope - see Alternatives.

## Detailed design

### 1. Directory restructure

Source tree move (`git mv`, content unchanged):

| From               | To                       |
|--------------------|--------------------------|
| `datadisk/tests/`    | `datadisk/tests/core/`     |
| `datadisk/xtests/`   | `datadisk/tests/xtests/`   |
| `datadisk/rtests/`   | `datadisk/tests/fbtests/`  |
| `datadisk/errtests/` | `datadisk/tests/errtests/` |

`datadisk/xsamples/` (oversize showcases) is **not** a test tier and
stays where it is; it ships under `XSAMPLES/`, not `TESTS/`.

On-disk (`build_data_po.sh`): the `add_swifts <subdir> <dir> [limit]`
helper already takes the destination ProDOS path as its first argument,
so the change is just the destination strings - `TESTS/CORE`,
`TESTS/XTESTS`, `TESTS/RTESTS`, `TESTS/ERRTESTS` - and confirming
AppleCommander's `-p` auto-creates *both* path levels on first put (it
creates the leading dir today for the one-level case; the two-level case
must be verified - see open questions). The per-tier staging limits are
unchanged (CORE/XTESTS keep the 2 KB Family-A cap; RTESTS/ERRTESTS use
limit 0, streamed by the Compiler).

Makefile: rename the four `*_TESTS_DIR` vars' values to the new paths and
update `TEST_SWIFTS` globs to `datadisk/tests/*/*.swift`. The
`disk-data` recipe's argument order to `build_data_po.sh` is unchanged.

Docs to touch: `datadisk/README.md`, the data-disk build script header,
each tier's per-folder README, `docs/testing/TESTING.md`, and the Makefile
comment block. The tests themselves are byte-identical.

### 1b. Where the sequencer lives - a separate `TESTRUN.SYSTEM` tool

*Added 2026-06-17 after a budget check.* The auto-sequencer can **not** live
inside the boot launcher: the launcher's `BSS` segment overlays `ONCE` and
grows up to `$BF00 − __STACKSIZE__`, so it is RAM-bound (not file-size-bound),
with only **~466 B** of slack below the `$BF00` ceiling (`make size`'s
"5,314 B headroom" is file-vs-40,704, which is not the binding limit). The
~2 KB sequencer overflowed `BSS` by 1675 B. Reclaiming ~1.8 KB would mean
cutting existing launcher/editor features.

So the sequencer ships as its own SYS tool, **`TESTRUN.SYSTEM`**, exactly as
`DEBUG.SYSTEM` already does for the diagnostic - a standalone `$2000` SYS
binary with the *full* `$2000–$BF00` window to itself. It **links the proven
`boot_launcher_asm.s`** (so it reuses the exact MLI verbs, hardware probes,
and chain loaders - `a_install_and_chain` and the SWIFTSAT/SWIFTAUX
chunked-staging variants - rather than re-deriving that hard-won asm) and
supplies the 16 C globals that asm imports. The launcher gains only ~250 B
(fits the 466 B slack):

- a "Run tests (data disk)" launcher menu action that chains `TESTRUN.SYSTEM`, and
- a boot-time check: if a `TESTRUN` note exists on an online volume, chain
  `TESTRUN.SYSTEM` instead of opening the browser.

Control flow per test: launcher (started from the menu, or boot-resume) → `TESTRUN.SYSTEM`
picks the next test, stages it, advances the `TESTRUN` note, and chains this
disk's interpreter → the test runs → Ctrl-D cold-reboots to the launcher →
the launcher sees the note → chains `TESTRUN.SYSTEM` again. When the queue
empties, `TESTRUN.SYSTEM` deletes the note, shows the summary, and chains the
launcher back. `TESTRUN.SYSTEM` is machine-independent (one build for all
disks, like `DEBUG.SYSTEM`); it detects this disk's sole interpreter by
probing `file_on_disk` for SWIFTSAT/SWIFTAUX/COMPILER/SWIFTIIP/SWIFTIIE.

### 1c. Interactive front-end (TESTRUN.SYSTEM screens)

*Added 2026-06-18.* `TESTRUN.SYSTEM` is a small interactive app, not a silent
sweeper. Because it runs once per test (relaunched after each reboot), the
screens key off the queue state in the `TESTRUN` note:

- **Intro** (fresh start only): what the harness does, what to expect, and the
  keys. Any key continues; **Esc → main menu** (chains the launcher).
- **Tier selection** (only when the disk has ≥2 runnable tiers - i.e. an extras
  REPL disk with CORE + XTESTS): a checklist, **all selected by default**,
  number keys toggle, **Enter or the right-arrow (`$15`) starts**, **Esc → main
  menu** (the right-arrow mirrors the launcher menu/pickers; prompt:
  `RET/-> = RUN SELECTED`). The chosen set is a
  bitmask stored in the note; single-tier disks (lite REPL = CORE, Family B =
  FBTESTS) skip this screen.
- **Countdown** (before each test): `Test N of M: NAME`, a short 3-2-1
  countdown (poll `a_kbd`; any key skips the wait, **Esc → results**), then
  stage + chain. `M` is the total `.swift` count across the selected tiers,
  computed once at start and stored in the note.
- **Results** (queue empty, or Esc-terminated): **Esc → main menu**. Content is
  tier-dependent (per the 2026-06-18 decision): REPL tiers show `Ran X of Y`
  (no verdict channel - see Alternatives); Family B reads the `TESTLOG` the
  Runner wrote and shows per-test **PASS/FAIL**, `Ran X of Y, P pass F fail`.

Note body grows to `[selmask][tier][ord][countLo][countHi][total]`. The Runner
auto-advance (1c's countdown is in TESTRUN, but the *result-read* pause stays in
the Runner since the Family B result is on the Runner's screen) keeps the marker
mechanism from section 3.

### 2. REPL-tier approach - the auto-sequencer

**Tier → binary mapping (which tiers a disk can run).** The booted disk
fixes the available binary, so the harness runs only the tiers that
binary supports:

| Booted disk            | Binary on it        | Tiers it runs        |
|------------------------|---------------------|----------------------|
| II+/​//e lite REPL       | SWIFTIIP / SWIFTIIE | `CORE`               |
| Saturn / aux extras REPL | SWIFTSAT / SWIFTAUX | `CORE`, `XTESTS`     |
| compiler (Family B)    | Compiler + Runner   | `CORE`, `XTESTS`, `FBTESTS` |

*Revised 2026-06-18: run every tier the binary can ("run all where possible").
The compiler-runner is a superset of the REPL - it runs the general `CORE` and
extras `XTESTS` tests too (the fbtests README confirms `XCONV`/`XARRAY` run on
it), not just its own `FBTESTS`. (`ERRTESTS` stays excluded - deliberately
failing, not self-checking.)*

`ERRTESTS/` is not self-checking (the programs fail on purpose to show an
error message). Default: exclude it from the automated run; optionally
offer it as a separate "show each error" walk on compiler disks. The
harness already knows the disk family at build time (the `WITH_*` /
`LITE_*` flags that pick which binary the launcher chains), so the
tier set is a compile-time constant per launcher - no runtime probe.

**The run loop, across reboots.** The launcher cannot stay resident while
a test runs (the interpreter overwrites low memory and the run
cold-reboots), so the queue lives in the resume note, exactly like
`LASTRUN`:

1. Run tests started → launcher resolves the tier list, and for the first
   tier enumerates its subdirectory with the existing dir-read MLI verbs
   (`a_mli_read_dirblk` et al., already used by the browser).
2. It writes a `TESTRUN` note (a new variant of the `LASTRUN`
   serialization): the active tier, the entry cursor, and a running
   count. Rather than serialize every filename, store *tier + ordinal*
   and re-enumerate on each resume - ProDOS directory order is stable, so
   "the k-th `.swift` in `TESTS/CORE`" is a deterministic cursor and the
   note stays tiny (fits the existing 96-byte `g_note`).
3. It launches the k-th test the normal way - stage source into the 2 KB
   buffer for a Family-A tier, or hand the Compiler the path for a
   Family-B tier (the two existing run paths; no new launch mechanism).
4. The interpreter runs it; output (including the `fail 0` sentinel) is
   on screen. The human reads it and presses the advance key; `:quit`
   cold-reboots to the launcher as it does today.
5. On boot the launcher finds a `TESTRUN` note (not `LASTRUN`), advances
   the cursor (next ordinal, or first ordinal of the next tier), and goes
   to step 3 - *without* drawing the browser. When the last tier's last
   entry is done it deletes the note and shows "ran N tests - check for
   any FAIL above" plus returns to the menu.

**Human gesture per test.** *Decided: minimal, via Ctrl-D (zero interpreter
cost).* After a staged test runs, the REPL drops to its empty interactive
prompt; **Ctrl-D** there already exits to the launcher on all four binaries
(keyboard.c:279 - EOF trips the `len <= 0` break in repl.c, "same exit as
`:quit`"). So the per-test gesture is a single keystroke - read the screen,
press Ctrl-D - and the launcher's auto-continue does the rest. No interpreter
change at all; only the launcher learns to read a `TESTRUN` note instead of
`LASTRUN`. (A "polished" in-interpreter `RET=next/ESC=stop` prompt was
considered and dropped: it would cost a handful of bytes in each at-ceiling
binary to save nothing Ctrl-D doesn't already give.)

Either way the navigation cost - the thing that dominates today - is gone.

### 3. Compiler-runner approach - Family-B results log (follow-on)

Goal: on compiler-runner disks, remove the human from the loop and leave
a persistent report, without rewriting the test programs.

Mechanism: a "test mode" flag handed to the Runner (e.g. a byte in the
`TESTRUN` note the launcher already maintains). When set, the Runner's
`print` path watches the emitted text for the failure token: the
self-checking convention is that a clean run's tally line reads
`fail 0` and a dirty one contains `FAIL`/`fail N>0`. The Runner keeps a
single "saw-failure" flag, and on program exit appends one line -
`<NAME>: PASS` or `<NAME>: FAIL` - to `TESTLOG` on the data disk via the
existing `appendFile` MLI path (doc 017). The test sources stay
unchanged; only the Runner gains a guarded flag + one append on exit.

The launcher, in Family-B test mode, deletes any stale `TESTLOG` at the
start of a run and, at the end, reads it back and prints the verdict list
(and a `PASSED k/N` summary), so a compiler-disk run is fully automated
and produces a report you can also pull off the disk image host-side.

This deliberately covers only Family B. The general/extras REPL tiers
keep the REPL-tier approach (human-verified) because making them
auto-verify would require either the rejected screen-scrape or giving the
REPLs a file writer they structurally cannot afford.

### Files this will touch

- `datadisk/` - directory move (4 dirs) + READMEs.
- `tools/host/diskimg/build_data_po.sh` - destination paths, header comment.
- `Makefile` - `*_TESTS_DIR` values, `TEST_SWIFTS` glob, comments.
- `tools/apple2/testrun_sys/testrun.c` + `testrun.cfg` (new) - the sequencer itself:
  tier resolution, `read_dir` enumeration, staging, `TESTRUN` note read/write,
  the per-test chain and the end-of-run summary. Links the shared
  `tools/apple2/boot_launcher/boot_launcher_asm.s` for the MLI verbs + chain loaders.
- `tools/apple2/boot_launcher/boot_launcher.c` - only the two ~minimal hooks:
  `chain_testrun` (the Run-tests menu action) and a `TESTRUN`-note probe folded into
  `note_consume`'s existing online-volume scan (boot-resume). Plus the
  `note_build_path` tag parameter (shared by `LASTRUN`/`TESTRUN`).
- `Makefile` (TESTRUN.SYSTEM target + per-disk wiring; `FAMILYB_SAMPLES`
  trimmed to fit it on the tight compiler disks), `tools/host/diskimg/build_po.sh`
  (`TESTRUN_BIN`).
- *(compiler-runner approach only)* `src/file_runner/` - test-mode flag,
  saw-failure flag in the print path, append-on-exit; and the harness's log
  read-back.
- `docs/testing/TESTING.md`, `docs/contributing/ROADMAP.md` (Phase 17 entry).

## Alternatives considered

- **Screen-scrape approach - auto-verify all tiers (rejected:
  architecturally impossible on the REPL tiers).** The idea was to have the
  interpreter
  return *preserving* the text page ($0400–$07FF) so the launcher could
  scan it for `FAIL`/`fail 0`, auto-verifying all tiers uniformly. Code
  review of the return path (`platform_shutdown`, screen.c:570-573) kills
  it for Family A: `:quit` deliberately forces a **cold** start (it zeroes
  the `$03F4` power-up byte and `jmp ($FFFC)`), which clears memory and the
  screen before the launcher ever runs - so there is no window to scrape.
  The cheaper variants are blocked for the same root cause: a running REPL
  has **overwritten the language-card MLI with its own LC segment**
  (screen.c:549-552), so it can neither MLI-re-chain back to the launcher
  with the screen intact, nor write a verdict file. The *only* channel out
  of a REPL is the screen, and the mandatory cold reboot erases it. The
  screen-scrape approach is therefore not deferred but **rejected** for the
  REPL tiers; the compiler-runner approach works on Family B precisely
  because the Runner keeps MLI resident and can persist a verdict to disk
  that survives the cold reboot.
- **Concatenate the suite into one program, run once (rejected).** No
  reboot, no note - but blocked four ways: the 2 KB Family-A staging cap,
  the function-count limit, name collisions across files (every test
  defines `chk`, `npass`, …), and the absence of any include/`import`
  mechanism. Not viable for the full set.
- **Host-only, do nothing more (rejected).** `make test`/`sim` already
  cover the logic host-side; the entire point is the *target* gap they
  cannot reach. Doing nothing leaves the v1.0 real-HW gate as a manual
  slog.

## Cost

- **Memory:** the REPL-tier approach is essentially launcher-only (tier
  table, the Run-tests handler, `TESTRUN` note read/write, auto-continue) - the
  launcher is where headroom exists, and re-enumerating from a tier+ordinal
  cursor keeps the note inside the existing 96-byte `g_note`. The
  minimal-gesture variant adds **zero** interpreter bytes. The
  compiler-runner approach adds a guarded flag + one `appendFile` on exit
  to the Runner (Family B has KBs of image headroom) and a log read-back to
  the launcher.
- **Performance:** none at runtime; the harness only affects the dev
  testing loop (which it shortens substantially).
- **Code complexity:** modest. The reused pieces (dir enumeration, the
  two run paths, the reboot-resume note) already exist; the new logic is
  a queue cursor and a tier table. The compiler-runner approach adds one
  cross-binary convention (the failure token) that must stay in sync with
  the tests' sentinel wording.
- **Schedule:** restructure + REPL-tier approach ≈ a day; compiler-runner
  approach a few hours on top once the REPL-tier sequencer is proven.

## Migration

- **Tests:** unchanged content; only their on-disk/source paths move.
- **Disk images:** the data disk's tier directories become
  `TESTS/CORE`, `TESTS/XTESTS`, `TESTS/RTESTS`, `TESTS/ERRTESTS`. The
  `*-2disk` run targets and the per-tier READMEs update to the new paths.
  Anyone with an old data disk image just rebuilds it (`make disk-data`).
- **Notes:** `TESTRUN` is a new note kind alongside `LASTRUN`; a launcher
  that sees neither behaves exactly as today. Clean addition, no break.
- **Clean break vs deprecation:** the directory move is a clean break in
  paths only; nothing reads the old flat paths except the build, which is
  updated in the same change.

## Open questions

1. **Name for the general tier.** *Resolved 2026-06-17:* `CORE/` (the
   restructure has shipped - `datadisk/tests/core/` → `TESTS/CORE/`).
2. **AppleCommander two-level auto-create.** *Resolved 2026-06-17:* `ac -p
   out TESTS/CORE/NAME TXT` auto-creates **both** `TESTS/` and `TESTS/CORE/`
   on the first put (verified against `tools/host/AppleCommander-ac.jar` -
   listing shows the nested `TESTS/ → CORE/ → NAME`). No explicit mkdir
   step needed; the build-script change is purely the destination strings.
3. **Advance gesture (REPL-tier approach).** *Resolved 2026-06-17:*
   **minimal**, via **Ctrl-D**. Ctrl-D on the empty prompt that follows each
   staged run already exits the REPL to the launcher on all four binaries
   (keyboard.c:279, "same exit as `:quit`"), so advancing is a single
   keystroke at zero interpreter-byte cost - the polished prompt's only edge
   (keypress early-stop) isn't worth bytes the at-ceiling REPLs lack.
4. **ERRTESTS in the run.** *Resolved 2026-06-17:* **skip** them in the
   Run-tests auto-run (they deliberately fail, so they'd permanently pollute the
   pass/fail summary). No separate error-demo walk for now - the file browser
   already reaches `TESTS/ERRTESTS` for the manual eyeball pass; revisit only
   if the Run-tests infrastructure makes it trivial.
5. **Which disks get Run tests.** *Resolved 2026-06-17:* **all** launchers, with
   the command active only when a `TESTS/` tree is found in drive 2 (a
   launcher with no data disk has nothing to run). Shared launcher code, no
   per-disk build divergence.
6. **Compiler-runner failure token.** *Resolved 2026-06-18:* keep the
   free-form scan. The Runner watches its print output for the uppercase
   `FAIL` token (`chk()` prints it only on a failed check; the `fail 0` tally
   is lowercase, so it never false-triggers) - no canonical `@PASS`/`@FAIL`
   line, so no per-FBTESTS source churn. See the `TESTLOG` slice below.

## Decision

**Accepted - REPL-tier sweep built as `TESTRUN.SYSTEM` (2026-06-18).** The
directory restructure and the auto-sequencer are implemented and build clean;
the interpreters stay byte-identical (launcher-/tool-side only). The sequencer
lives in the standalone `TESTRUN.SYSTEM` (~7.7 KB with the interactive UI)
because the launcher is RAM-full.

**Placement (revised 2026-06-18): TESTRUN.SYSTEM ships on the DATA disk, not the
program disks.** It can only do anything with that disk's `TESTS/` tree, and
the interactive UI grew it past what the tight Saturn compiler disk could hold.
The data disk has ~49 KB free, and its volume name is fixed (`SWIFTII.DATA`),
so the launcher's Run-tests action chains the constant absolute path
`/SWIFTII.DATA/TESTRUN.SYSTEM` - no ON_LINE scan, so it fits the RAM-full
launcher (II+ ~160 B / //e ~220 B slack). The interpreters live on the boot
volume, so `chain_testrun` restores the boot prefix first (the harness inherits
it). The program disks keep their full sample sets (`FAMILYB_SAMPLES` restored).

(Architecture chosen 2026-06-18: keep the Run-tests launcher command - the
"end-user, run-tests-from-your-disk" model - over the alternative of making the
test disk bootable and driving the interpreter from drive 2.)

The per-test advance is tier-correct: **Family A (REPL)** ends at the prompt →
**Ctrl-D**; **Family B (Runner)** has no Ctrl-D, so the Runner **auto-advances
after ~4 s** in a sweep instead of waiting for a key. The signal is a 2-byte
marker `TESTRUN.SYSTEM` writes at `$1B08` (config.h `TESTRUN_MODE_ADDR`) before
chaining a Family B test; it rides the `$1B00` handoff page through the
COMPILER→RUNNER chain exactly as the Saturn slot at `$1B04` does, and the
Runner consumes it on read so a later normal `[X]` run still pauses for a key
(`runner_main.c`). All TESTRUN.SYSTEM + Runner output is ≤ 40 columns.

**Family B PASS/FAIL - shipped 2026-06-18 (the `TESTLOG` slice).** On a
compiler-runner the Runner watches its own print output for the uppercase
`FAIL` token (the self-checking tests' `chk()` prints it only on a failed
check; the `fail 0` tally is lowercase, so it never false-triggers) and appends
one `P`/`F` byte per test to `/SWIFTII.DATA/TESTLOG`, in run order. TESTRUN.SYSTEM
clears that log at a Family B sweep start and, on the results page, reads it
back and **lists the failed tests by name** (`PASSED p, FAILED f` + the names).
The watcher is gated `WITH_TESTLOG` (Runner only - every other binary stays
byte-identical) in `screen.c`'s `emit()` chokepoint; it cost a 256 B Runner
heap trim per variant (a sanctioned runtime-data-capacity trade). REPL tiers
still show only `Ran X of Y` - they have no MLI to write a verdict.

## Automated driver - `tools/host/acceptance/` (2026-06-19)

The sweep designed above still required a human to boot each emulator config and
press keys. That outer loop is now itself automated by
[`tools/host/acceptance/`](../../../tools/host/acceptance) (`make acceptance`), so the whole
self-checking suite runs across the matrix unattended:

- **Backend: izapple2's `headless` frontend.** It has embedded ROMs (nothing to
  source) and a
  deterministic stdin protocol: `run <cycles>` advances an exact number of CPU
  cycles then pauses, `key`/`type`/`enter` inject keystrokes, `text` dumps the
  screen as ANSI, `png` snapshots the framebuffer. Same machine-config flags as
  `izapple2sdl`, so it reuses the profile→flags map from `run_izapple2.sh`.
- **What it drives.** For each config it boots the disk, selects **Run tests**
  (the numbered menu entry - option 4 on a REPL disk, 3 on a compiler disk; the
  harness reads the number off the menu so it adapts per
  disk), works through the sweep, and collects the verdict. Family A (REPL)
  tiers advance on **Ctrl-D** (`key 4`) and are verified by scraping each test's
  on-screen `PASS n FAIL 0` tally; Family B reads the section-3 **`TESTLOG`**
  back off the data-disk image with AppleCommander. Graphics / 80-col capture
  PNGs (the text screen can't show them).
- **Verified on real izapple2 (2026-06-19):** `iip` CORE sweep 10/10; the
  Family B `compiler` config reports `TESTLOG: 20 pass, 0 fail` end-to-end
  (Compiler→Runner→`.swb`→`TESTLOG`); the graphics snapshot captures the
  rendered GR drawing. Notes for whoever extends it: the machine must be
  `pause`d once before the first `run` (startup race); the intro's "ANY KEY"
  wants a *printable* key (space), not Return; boot is ~150M cycles and each
  test is a full reboot, so a sweep is minutes of fast-mode emulation.
