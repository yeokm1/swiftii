# BUILDING.md

How to build SwiftII on macOS, from a clean machine to a running
emulator.

---

## Prerequisites

- macOS 13 or later (Apple Silicon or Intel)
- Xcode Command Line Tools (`xcode-select --install`)
- Homebrew (<https://brew.sh>)
- ~500 MB free disk space

---

## One-shot setup

The simplest path is the setup script followed by a full CI run:

```sh
./scripts/setup.sh
make ci          # full local gate; everything green
make run         # boots Mariani → "hello, world"
```

No manual `export` lines needed - the Makefile auto-detects the
AppleCommander jar (`tools/host/AppleCommander-ac.jar`) and Homebrew's
keg-only Java (`/opt/homebrew/opt/openjdk@21/bin`) so `make disks`
works in a fresh shell.

If you want to do the setup manually, read on.

---

## Manual setup

### 1. Standard Unix tools

```sh
brew install make python3 git
```

The macOS-bundled `make` is recent enough; you don't need GNU make
specifically, but if you have it, the Makefile works with both.

### 2. cc65

cc65 is a complete C cross-development suite for 6502-based systems.
It's been the standard 8-bit C toolchain for ~25 years and has solid,
well-trodden Apple II support.

```sh
brew install cc65
```

Verify:

```sh
cl65 --version
ca65 --version
ld65 --version
```

`cl65` is the driver that calls `cc65` (compiler), `ca65` (assembler),
and `ld65` (linker) in sequence. Most of the time you invoke `cl65`
and it figures out the rest.

### 3. py65 (6502 simulator for `make sim`)

```sh
pip3 install py65
```

`py65` lets us run compiled 6502 code headlessly. It doesn't simulate
the Apple II's video, keyboard, ProDOS, or anything else - just the
bare CPU and a configurable memory map. Perfect for unit-testing the
bytecode VM and the codegen of small functions.

### 4. AppleCommander (for building disk images)

There's no Homebrew formula for AppleCommander, so install it manually:

1. Make sure Java is available. Homebrew installs it keg-only (not on
   `PATH` by default), but the Makefile handles that automatically. If
   Java isn't installed at all:
   ```sh
   brew install openjdk@21
   ```
2. Download the **CLI** build (`AppleCommander-ac-X.Y.Z.jar`, **not** the
   GUI jar) from the releases page:
   <https://github.com/AppleCommander/AppleCommander/releases>
3. Place it at `tools/host/AppleCommander-ac.jar` in the repo root. That's
   where `scripts/setup.sh` puts it and where the Makefile looks by
   default - no `export APPLECOMMANDER_JAR` needed.

The disk-image script shells out via `java -jar "$APPLECOMMANDER_JAR"`.
Override `APPLECOMMANDER_JAR` on the `make` command line if you want to
use a jar from a different path.

Used by `tools/host/diskimg/build_po.sh` to inject the boot selector (installed as
`SWIFTII.SYSTEM`) + the binaries that program disk carries
(`SWIFTIIP`/`SWIFTSAT`/`SWIFTIIE`/`SWIFTAUX`.SYSTEM, or the Family B
`COMPILER`/`RUNNER`.SYSTEM) into a bootable ProDOS `.po` image. SwiftII ships
**eight** images in two families - the REPL set (four interpreter program
disks + one non-boot data disk) and three Family B compiler disks (see
section *Disk layout* below).

### 5. ProDOS 2.4.3 system files

For the bootable `.po` images we need a ProDOS 2.4.3 disk to use as a
template (`build_po.sh` copies it and drops our SYSTEM files onto it). The
setup script downloads the bootable image and places it at
`tools/host/diskimg/prodos243/ProDOS_2_4_3.po`. If you'd rather do it
manually:

1. Download `ProDOS_2_4_3.po` from the John Brooks revival distribution
   (`https://releases.prodos8.com/ProDOS_2_4_3.po`).
2. Save it as `tools/host/diskimg/prodos243/ProDOS_2_4_3.po`.

We don't commit this image to the repo (out of an abundance of
caution about license clarity), but it's freely redistributable and
easily fetched.

### 6. An emulator

Pick one. SwiftII's `make run` defaults to Mariani; change
`emulator/run.sh` if you prefer something else.

> **`make run-configs`** prints the full hardware test matrix - every
> `run-*` target, the machine it sets up, which SwiftII binary it should
> select, and the per-emulator caveats. The written version with expected
> results (incl. edge/negative cases) is **`docs/testing/TESTING.md` section Emulator
> hardware configurations**. Start there.

- **Mariani** - modern macOS-native fork of AppleWin. Best for
  day-to-day use. <https://github.com/sh95014/AppleWin>
- **izapple2** - CLI-configured profiles (Saturn 128K, //e), with embedded
  ROMs and no GUI to set up. See section 6a.

> A bare-64K //e and a basic-80-col (1K) //e aren't modelled by either
> supported emulator, so those configs are **real-hardware-only**.
>
> An II+ 80-column path via the **Videx Videoterm** (slot 3) is
> **REPL/program only**: the REPL path is `SWIFTSAT`, so it requires Saturn
> plus Videx (opt-in `text80()`/`text()`). The Family B `RUNNER` can also drive
> a Videoterm for program output on the flat II+ tier. The lite REPL, launcher,
> and editor stay 40-column (RAM-walled). Smoke-test the REPL path with
> `make run-iz-videx-2disk` (izapple2 II+ Saturn + Videx - output routing
> only; its bundled Videoterm ROM diverges, so real hardware is the test). See
> docs/contributing/design/013-80col-text.md (track B).

#### 6a. izapple2 - CLI-configured profiles

[izapple2](https://github.com/ivanizag/izapple2) models a Saturn 128K,
ships with **embedded ROMs** (nothing to source or version-match) and is
a single self-contained binary - the easiest way to run the II+ binaries
from the command line.

**On Apple Silicon there's nothing to install** - the `izapple2sdl_mac_arm64`
binary is committed at the repo root, and the `run-iz-*` targets auto-detect it.

On other platforms, download the `izapple2sdl` binary for macOS from the
[releases page](https://github.com/ivanizag/izapple2/releases) (Intel: the
matching `izapple2sdl_mac_*`), `chmod +x` it, and either put `izapple2sdl` on
PATH or `go install github.com/ivanizag/izapple2/...@latest`. The Makefile falls
back to `izapple2sdl` on PATH when the repo-root binary is absent. Override
either with `make run-iz-iip IZAPPLE2=/path/to/it`.

```sh
make run-iz-iip        # -model=2plus -s0 language   iip-lite.po -> SWIFTIIP
make run-iz-sat        # -model=2plus -s0 saturn     iip-sat.po  -> SWIFTSAT
make run-iz-iie        # -model=2e                   iie-lite.po -> SWIFTIIE
make run-iz-iienh      # -model=2enh                 iie-aux.po  -> SWIFTAUX
#   SWIFTAUX needs a //e with a 64K ext-80-col card (128K). izapple2's //e is
#   always 128K, so 2enh supplies that aux RAM; the enhanced (65C02) model also
#   doubles as a 65C02-compat check. The card is the requirement, not 65C02.

# Same configs with the data disk auto-mounted in drive 2 (samples + TESTS/) -
# the form the test matrix uses, so you can run programs once the config boots:
make run-iz-iip-2disk        # SWIFTIIP + data disk
make run-iz-sat-2disk        # SWIFTSAT + data disk (run the x* extras tests here)
make run-iz-videx-2disk      # SWIFTSAT + Saturn + Videx + data disk (80-col smoke)
make run-iz-iie-2disk        # SWIFTIIE + data disk
make run-iz-iienh-2disk      # SWIFTAUX (//e + ext-80-col aux; 2enh is 65C02) + data disk
```

For Mariani the data-disk variants mirror the izapple2 `-2disk` suffix:
`make run-mari-iip-2disk` / `run-mari-sat-2disk` / `run-mari-iie-2disk` /
`run-mari-aux-2disk` (one per program disk; set the matching model in Mariani's
GUI). Compiler-runner disks have the same scheme:
`run-mari-compiler[-iie|-sat]` and their `-2disk` data-disk variants.

izapple2 **runs SWIFTSAT** on its Saturn (input + output confirmed). (One
known cosmetic issue: keyboard echo is invisible while typing on SWIFTSAT under
izapple2 - input still captured; see `docs/testing/TESTING.md`.)
izapple2's //e (`2e`/`2enh`) is always 128K; `run-iz-iie` boots the //e lite
disk (SWIFTIIE) and `run-iz-iienh` the //e aux disk (SWIFTAUX) - bare-64K /
basic-80-col //e need real hardware. Plus the edge cases
`run-iz-iip48` (48K, no boot), `run-iz-sat-s4` (Saturn in slot 4),
`run-iz-memexp` (a RAM card must not false-trigger extras). Override the
binary with `IZAPPLE2=…`; mapping lives in `emulator/run_izapple2.sh`.

#### 6b. The automated acceptance harness (izapple2 `headless`)

izapple2's GUI binary (and Mariani) can't be driven from a script, but izapple2
also builds a **`headless`** frontend - same embedded ROMs and machine-config
flags, plus a deterministic stdin protocol (`run <cycles>`, `key`/`type`,
`text`, `png`). That backs the automated acceptance harness in
[`tools/host/acceptance/`](../../tools/host/acceptance): `make acceptance` boots every
config, injects keystrokes, runs the launcher's **RUN TESTS** sweep (or a
graphics / 80-col scenario), and reports pass/fail across the whole matrix -
Family B verdicts read back from the on-disk `TESTLOG`.

Build the binary once (needs Go), then run the harness:

```sh
make acceptance-build              # go install …/izapple2/frontend/headless@latest
make acceptance                    # the whole matrix
make acceptance CONFIGS="iip sat"  # just these configs
make acceptance ARGS=--dry-run     # print the plan, launch nothing
make acceptance ARGS=--window      # interactive GUI window (browser): watch + control
make acceptance ARGS=--show        # live text screen in the terminal instead
make acceptance RELEASE=releases/v1.0.0  # run pre-built disks, skip the build
make acceptance-list               # list the configs
```

`acceptance-build` drops a `headless` binary in `$(go env GOPATH)/bin`; the
harness finds it there, on `PATH`, or via `IZAPPLE2_HEADLESS=…`. No romsets to
source (embedded).

> **The `headless` frontend is not in izapple2's prebuilt downloads — you compile
> it yourself.** izapple2's GitHub releases ship only the SDL2/console
> single-file executables; the `headless` command lives at
> [`github.com/ivanizag/izapple2/frontend/headless`](https://github.com/ivanizag/izapple2/tree/master/frontend/headless)
> and is built from source by `make acceptance-build` (a thin wrapper over `go
> install …/frontend/headless@latest`). It carries no tagged release — `@latest`
> resolves to a pseudo-version off `master` — so **recompile it** (`make
> acceptance-build` again) to pick up upstream fixes or after a `go clean
> -cache`. This is the same `headless` binary the doc-screenshot tool
> ([`tools/host/screenshots/`](../../tools/host/screenshots)) uses. `RELEASE=<dir>` runs against a pre-built image set (e.g. a
tagged `releases/v<version>/`) instead of building fresh — the `make` build
step is skipped. Both program and data disks are always copied into
`build/acceptance/<config>/` and run from there, so the source images (a
release included) are never modified. `--window` opens a browser window mirroring the emulator's
rendered screen and doubles as a controller - tick/untick configs, press Start,
stop the current or all tests, with live stage/keys and time-remaining
estimates. Harness internals (incl. the full `--window` rundown):
[`tools/host/acceptance/README.md`](../../tools/host/acceptance/README.md).

### 7. clang and clang-format (host builds + style)

```sh
brew install clang-format
```

The system `clang` is fine for host-side builds.

---

## Building

From the repo root:

```sh
make test                   # host-side unit tests under clang
make sim                    # bytecode tests on py65
make apple2                 # cross-compile II+ lite interpreter (SWIFTIIP) via cc65
make apple2-iie             # cross-compile //e lite interpreter (SWIFTIIE)
make apple2-swiftsat        # cross-compile Saturn 128K extras (SWIFTSAT)
make apple2-swiftaux        # cross-compile //e-aux extras (SWIFTAUX)
make apple2-all             # all four interpreter binaries
make apple2-compiler        # Family B Compilers: II+ Tier-1 + //e Tier-3 (aux-paged)
make apple2-runner          # Family B Runners:   II+ + //e RUNNER.SYSTEM
make apple2-saturn-familyb  # II+ Tier-2 (Saturn-paged) Compiler + Runner
make apple2-familyb         # all three tiers' Compilers + Runners
make disks                  # build all 8 disks (the five Family A + three Family B)
make disk-iip-lite-repl          # II+ lite   (build/disk/swiftii-iip-lite-repl.po)
make disk-iip-sat-repl           # II+ Saturn (build/disk/swiftii-iip-sat-repl.po)
make disk-iie-lite-repl          # //e lite   (build/disk/swiftii-iie-lite-repl.po)
make disk-iie-aux-repl           # //e aux    (build/disk/swiftii-iie-aux-repl.po)
make disk-data              # non-boot DATA disk: samples + tests (swiftii-data.po)
make disk-iip-compiler      # II+ Tier-1 Family B compiler disk (swiftii-iip-compiler.po)
make disk-iie-compiler      # //e Tier-3 Family B compiler disk (swiftii-iie-compiler.po)
make disk-iip-sat-compiler  # II+ Tier-2 Saturn Family B compiler disk (swiftii-iip-sat-compiler.po)
make disks-familyb          # just the three Family B compiler disks
make release                # build all 8 disks and copy them into releases/
make swbc                   # host .swift -> .swb compiler (build/host/swbc) - see "Host tools"
make run                    # launch emulator with the II+ lite disk
make run-sat / run-iie / run-aux   # the Saturn / //e-lite / //e-aux disk
make run-mari-iip-2disk     # II+ lite disk + the data disk in drive 2 (Mariani)
make run-iz-compiler[-iie]  # boot a Family B compiler disk (izapple2)
make run-mari-compiler[-iie|-sat]  # boot a Family B compiler disk (Mariani)
```

The project ships **two disk families** (8 images). **Family A** (the REPL
disks) is four single-interpreter program disks, each carrying the boot
launcher + exactly ONE interpreter + the demo programs (`SAMPLES/`), plus a
non-boot DATA disk with those samples and the on-disk test suite (`TESTS/`).
Every program disk also carries a `README.TXT` help file in its root (opened
from the File selector / editor; the launcher's About screen points to it) -
the II+ disks get an ALL-CAPS copy so it reads natively on a machine with no
lowercase glyphs, the //e disks a mixed-case copy.
**Family B** (design doc 015) is three **compiler disks** - launcher
(+ in-process editor) + the on-disk **Compiler** and **Runner**, no REPL
interpreter - for editing and running bigger programs that compile to `.swb`,
one per Compiler tier (II+ Tier-1 flat, II+ Tier-2 Saturn-paged, //e Tier-3
aux-paged). Because all three tiers ship `COMPILER.SYSTEM`/`RUNNER.SYSTEM`
under the same filenames, two disk-facing cues carry the tier: the launcher's
Family B **banner** (`SwiftII Compiler ][+` / `…][+ Saturn` / `…//e` - the
Saturn disk gets its own `-DFAMILYB_SATURN` launcher build, since the tier
can't be probed at run time) and the `README.TXT` **Runner line**, which names
that tier's required machine/card (flat = no extra card, Saturn = Saturn 128K,
//e = 64K aux), substituted per disk from `README_RUNNER` at disk-build time.

| `make` target      | image                      | launcher + binaries                       |
|--------------------|----------------------------|-------------------------------------------|
| `disk-iip-lite-repl`    | `swiftii-iip-lite-repl.po`      | II+ launcher + `SWIFTIIP.SYSTEM`          |
| `disk-iip-sat-repl`     | `swiftii-iip-sat-repl.po`       | II+ launcher + `SWIFTSAT.SYSTEM`          |
| `disk-iie-lite-repl`    | `swiftii-iie-lite-repl.po`      | //e launcher + `SWIFTIIE.SYSTEM`          |
| `disk-iie-aux-repl`     | `swiftii-iie-aux-repl.po`       | //e launcher + `SWIFTAUX.SYSTEM`          |
| `disk-iip-compiler`| `swiftii-iip-compiler.po`  | II+ launcher + Tier-1 `COMPILER.SYSTEM` + `RUNNER.SYSTEM` |
| `disk-iip-sat-compiler`| `swiftii-iip-sat-compiler.po` | II+ launcher + Tier-2 (Saturn-paged) `COMPILER.SYSTEM` + `RUNNER.SYSTEM` |
| `disk-iie-compiler`| `swiftii-iie-compiler.po`  | //e launcher + Tier-3 (aux-paged) `COMPILER.SYSTEM` + `RUNNER.SYSTEM` |
| `disk-data`        | `swiftii-data.po`          | full samples + `TESTS/`/`XTESTS/`/`FBTESTS/` (drive 2) |

On a **Family B** disk the editor's Ctrl-R (or `X` on a `.swift` in the file
selector) compiles the source to a `.swb` **next to it** and runs it; `X` on a
`.swb` runs the compiled program directly. The Compiler forks per tier (Tier 1
flat / Tier 2 Saturn-paged / Tier 3 aux-paged - see CONSTRAINTS.md "Family B
program-size limits") and, like the Runner, is per-machine for display: the //e
build is `WITH_IIE` (full-ASCII render) and the II+/Saturn builds use the pre-IIe
inverse-letter render with the `WITH_INVERSE_JM` `J`/`M` path-echo fix.

### Samples on disk

Source samples are split by **destination** so the source tree mirrors where
each file lands. Program/binary-disk sources live under `progdisk/`;
data-disk-exclusive sources live under `datadisk/`; no source file is duplicated
across the two. Within `progdisk/` they are further split by capability into
three folders, each copied to its own on-disk ProDOS directory (mirroring
`TESTS/`/`XTESTS/`): `progdisk/samples/` → `SAMPLES/` holds regular programs that
run on **any** system (incl. the lite REPL); `progdisk/xsamples/` → `XSAMPLES/`
holds the **`x`-prefixed** extras-REPL programs small enough to ship on a
program disk (graphics, speaker clicks, games - they run on the extras REPL or
any Family B Runner); `progdisk/fbsamples/` → `XSAMPLES/` holds the
**`x`-prefixed
Family-B-only** programs (`random`/`switch`/`for-in`), which reject on every
Family A REPL and so ship on the data disk only. The oversize paging showcases
live under `datadisk/xsamples/`. What ships where:

- **Lite disks** (`SWIFTIIP`/`SWIFTIIE`): the regular samples only - x-programs
  can't run on a lite REPL, so they're omitted.
- **Extras disks** (`SWIFTSAT`/`SWIFTAUX`): regular **+** the small extras-REPL
  samples (`xsamples/`). **Not** the Family-B-only ones - they'd reject on a REPL.
- **Family B compiler disks**: a **minimal inline set** (greet + functions +
  xsnake) so the Compiler keeps free blocks to write `.swb` output (a full disk
  surfaces as `write error err=$48 disk full`); the images keep ~2–6 KB free
  (II+ ~4 KB, //e ~5.6 KB, the Saturn tier tightest at ~2 KB).
- **Data disk** (drive 2): the canonical FULL set. Its build assembles `SAMPLES/`
  + `XSAMPLES/` from **all** the trees (program-disk samples - including the
  Family-B-only `fbsamples/` - are referenced from `progdisk/`, not copied into
  `datadisk/`) **plus** the data-disk-only oversize showcases that exceed the
  2 KB Family-A staging cap and so ship nowhere else, each landing on a
  different point of the [tier tradeoffs](design/020-tier2-saturn-paged-runner.md):
  - `xbig.swift` (9.1 KB number tour) wraps each section in a function so its
    bytecode flushes to the paged store and its arrays/strings free on return —
    so it compiles and runs on **all three tiers** (runtime peak ~1.7 KB, within
    the Saturn Runner's 1,792 B heap). Terse labels keep its const pool under the
    paged compile heap; the checksum is purely numeric, so they don't change it.
  - `xgrdemo.swift` (7 KB graphics) wraps each scene in a function, so its
    bytecode flushes to the bank — it compiles and runs on **all three tiers**.
  - `xfuncs.swift` is **function-heavy** — it only fits the **Tier-2/3** paged
    compilers (its total bytecode exceeds the flat buffer).

  Mount the data disk (the `*-2disk` run targets do) to browse/compile them; the
  `.swb` lands next to its source.

Boot the disk that matches your machine; the launcher chains whichever
interpreter is on it (its lite↔extras fallback is bidirectional, so the present
binary always wins). The split replaced the old two-disk layout once the II+
boot disk filled to **0 bytes free** - giving each binary its own disk reclaims
room for the launcher to grow (e.g. the file-browser preview polish) and now
leaves space for the demo `SAMPLES/` on every program disk, so a single-drive
user has runnable examples without the data disk. The dev-facing `TESTS/` stay
on the data disk only (they would refill the tight `iip-sat` image); mount it in
drive 2 (`make run-mari-iip-2disk`) to reach them.

`make` with no arguments builds everything (host + target) and runs
all tests but does **not** launch the emulator.

### What cc65 produces

`make apple2` produces the II+ lite binary in `build/apple2/`;
`make apple2-iie` the //e lite (a `WITH_IIE` build at
`build/apple2/iie/`); the extras binaries come from their own targets:

- `SWIFTIIP.SYSTEM` (`make apple2`) - the lite ProDOS SYS-format
  interpreter for II+ 16K LC and earlier (doc-003 //+ typing model,
  caps). Ships on the II+ lite disk (`make disk-iip-lite-repl`).
- `SWIFTIIE.SYSTEM` (`make apple2-iie`) - the //e lite (same sources,
  `-DWITH_IIE`): native //e case input + lowercase display. Ships on the
  //e lite disk (`make disk-iie-lite-repl`). See design doc 003 rev 4.
- Both are chained to by the boot selector (`SWIFTII.SYSTEM` on disk,
  built by `make boot-launcher`) - ProDOS 2.4.3 auto-launches the launcher, not
  these directly. With one interpreter per disk, the launcher chains whichever
  binary is present (bidirectional lite↔extras fallback).
- `SWIFTSAT.SYSTEM` (`make apple2-swiftsat`) - the Saturn 128K extras
  interpreter. Ships on the II+ Saturn disk (`make disk-iip-sat-repl`). The
  extras (`asc`/`chr`/`Int`, array methods, the `home`/`peek`/`poke`/`gr`/…
  platform builtins) run in place in Saturn bank 1 via the XLC mechanism
  (design doc 011).
- `SWIFTAUX.SYSTEM` (`make apple2-swiftaux`) - the //e-aux extras
  interpreter (64K extended-80-col card, no Saturn). Ships on the //e aux disk
  (`make disk-iie-aux-repl`). Same extras surface, reached by copying each XLC body
  down from an aux-RAM park into a main-RAM staging buffer per call
  (AUXMOVE-based).

(There is no unified `SWIFTIIX.SYSTEM`: SWIFTSAT + SWIFTAUX split the extras
role per machine. The `WITH_EXTRAS` umbrella lives on as SWIFTSAT's feature
set.)

`make apple2-familyb` produces the Family B tools (also
individually via `make apple2-compiler`, `make apple2-runner`, and
`make apple2-saturn-familyb`):

- `COMPILER.SYSTEM` - the standalone, MAIN-only compiler (empty LC so
  ProDOS's MLI survives): streams a `.swift` from disk through a 4 KB
  sliding window and writes a `.swb` next to the source (doc 015/016).
  Built in **three tiers** that grow the compilable program size by
  pushing bytecode out of MAIN (same `swiftii-compiler.cfg`, different
  flags):
  - **Tier 1** (II+ flat, `make apple2-compiler` → `build/apple2/compiler/`):
    bytecode stays in MAIN; smallest reach. Ships on `disk-iip-compiler`.
  - **Tier 2** (II+ Saturn, `make apple2-saturn-familyb`): `-DWITH_AUX_COMPILE
    -DBC_STORE_SATURN` pages bytecode into Saturn 128K banks. Ships on
    `disk-iip-sat-compiler`.
  - **Tier 3** (//e, part of `make apple2-compiler` → `build/apple2/iie/`):
    `-DWITH_AUX_COMPILE` pages bytecode into //e aux RAM. Ships on
    `disk-iie-compiler`.
- `RUNNER.SYSTEM` - the MAIN-only `.swb` runner (VM + runtime + all
  builtins inline; no compiler). Built per machine: a plain II+ build, a
  `-DWITH_IIE` //e build that pages bytecode through aux RAM
  (`-DWITH_AUX_BC`), and a Saturn-paged II+ build
  (`-DWITH_AUX_BC -DBC_STORE_SATURN`) for the Tier-2 disk - so each
  compiler tier has a matching Runner that reads back its paged `.swb`.

(There are **no** HGR binaries: no `SWIFTIIH.SYSTEM` /
`SWIFTIIF.SYSTEM` and no `WITH_HGR`/`WITH_FULL`/`WITH_AUX_DATA`
flags - HGR lives in ROADMAP "Maybe / probably
never" item 1.)

The editor is **not** a standalone binary: it lives inside the boot
launcher (`SWIFTII.SYSTEM`) and runs in-process - the browser's `[E]`/`[F]`
enter it directly, no chain. There is no `make apple2-edit` / `SWIFTED.SYSTEM`;
the launcher's size is covered by `make size`, and the portable editor modules
are unit-tested on the host (`make test`).

Compiled with `cl65 -t apple2 ...` using linker configs in
`src/platform/apple2/`:

- `swiftii-system.cfg` - lite interpreter binary
- `swiftsat-system.cfg` - SWIFTSAT (adds the Saturn bank-1 XLC region)
- `swiftaux-system.cfg` - SWIFTAUX (adds the copy-down staging region)
- `swiftii-compiler.cfg` / `swiftii-runner.cfg` - Family B tools
  (MAIN-only: the LC code-name segment runs from MAIN so ProDOS's MLI
  body survives for file I/O + chaining)

All binaries load at $2000 (the ProDOS SYS convention).

### Compile-time flags

The real build-selection flags are baked into the per-target recipes
(`-DWITH_EXTRAS`, `-DWITH_SWIFTSAT`, `-DWITH_SWIFTAUX`, `-DWITH_IIE`,
`-DWITH_SWB`, `-DLITE_IIE`, plus the Family B `COMPILER_DEFS` /
`RUNNER_DEFS` buffer sizes). The only user-togglable flag is:

```sh
make apple2-iie WITH_80COL=0   # compile the //e 80-col path out
                               # (byte-identical-when-off A/B check)
```

There are no per-feature umbrella flags (`WITH_CLOSURES`,
`WITH_DICT`, `WITH_HISTORY`, `WITH_HGR`, `WITH_FULL`,
`WITH_AUX_DATA`) - those features live in ROADMAP "Maybe / probably never".
Remember the cc65 trap: `make WITH_FOO=0` does **not** rebuild
anything by itself (mtime, not flags) - touch the affected sources
or `make clean` first (LESSONS).

`make ci` builds every ship binary - the four interpreters
(`apple2-all`), the Family B Compiler + Runners (`apple2-familyb`),
and the three launcher builds (II+, //e, and the II+ Saturn-compiler
variant) - then verifies size budgets for each and builds all eight disks.

### Sample cc65 invocation

For reference (the Makefile does this for you):

```sh
cl65 -t apple2 -O -Cl \
  -C src/platform/apple2/swiftii-system.cfg \
  -o build/apple2/SWIFTIIP.SYSTEM \
  -m build/apple2/swiftiip.map \
  -Ln build/apple2/swiftiip.lbl \
  src/main/main.c src/lexer/*.c src/compiler/*.c \
  src/vm/*.c src/runtime/*.c src/repl/*.c \
  src/file_runner/*.c src/platform/apple2/*.c \
  src/platform/apple2/mli.s
```

Key flags:

- `-t apple2` - target the original Apple II (not `apple2enh`,
  which requires a 65C02).
- `-O -Cl` - optimize for size; static locals where possible.
- `-C ...cfg` - custom linker config controlling memory layout
  (language card vs main RAM placement).
- `-m ...map` - emit a linker map for `make size` to read.
- `-Ln ...lbl` - emit symbol labels for the disassembler.

### Host-only build

For pure host-side work (lexer fixes, compiler logic, anything that
doesn't touch the platform layer):

```sh
make test
```

This builds with system `clang` and runs the unit tests. Fastest
possible loop - typically <1 second from edit to test result.

### Host tools

#### `swbc` - compile `.swift` → `.swb` on your Mac

You don't need the Apple II Compiler to produce bytecode. `swbc` is a
native host binary that links the **same** compiler + `.swb` writer as the
on-disk Compiler, so its output is byte-format-identical (same opcodes,
builtin ids, `SWB_VERSION`) - just without the on-disk Compiler's 1,834 B
size cap.

```sh
make swbc                          # → build/host/swbc
build/host/swbc IN.swift OUT.swb   # compile a program

# example
printf 'var x = 21\nprint(x + x)\n' > hello.swift
build/host/swbc hello.swift hello.swb
# swbc: hello.swift -> hello.swb  (bytecode 14 B, image 26 B)
```

Drop the resulting `.swb` on a data disk and run it with the Family B
**Runner** (`X` on the `.swb` in the launcher's file browser). Variants:

- `build/host/swbc IN.swift OUT.swb stream` - feeds source through the
  sliding `srcwin` window (matches the on-disk Compiler's refill path)
  rather than one whole buffer.
- `make swbc-aux` (→ `build/host/swbc_aux`) - built like the //e Compiler
  (`WITH_AUX_COMPILE`, 896 B scratch window). Use it to check **whether the
  on-target Tier 3 Compiler could actually compile a given program**: it
  errors "program too big" the same way the real Compiler would. Plain
  `swbc` (16 KB `FILE_BC_SIZE`) won't surface that limit.
- `make bigswb` - generates an oversized straight-line program and compiles
  it, to stress-test the Runner's aux paging.

#### `disasm` - disassemble a `.swb`

```sh
make disasm FILE=hello.swb         # dump opcodes of a compiled program
```

---

## Running on real hardware

The `.po` images are bootable on real hardware -
`swiftii-iip-lite-repl.po` / `swiftii-iip-sat-repl.po` on a real Apple II Plus,
`swiftii-iie-lite-repl.po` / `swiftii-iie-aux-repl.po` on a real //e. Boot the one that
matches your machine; it **auto-starts the launcher menu** (no manual
`-SWIFTII` needed), from which you reach the REPL, file selector, and editor.

Get the images prebuilt from the [`releases/`](../../releases) directory or the
[GitHub Releases](https://github.com/yeokm1/swiftii/releases) assets, or build
them with `make disks` (`make release` also copies them into `releases/`).

Options for getting an image onto real hardware:

1. **Floppy emulator** (BMOW **Floppy Emu**, CFFA3000, MicroDrive, …) -
   copy the `.po` files to the SD card, pick the image from the device menu,
   and boot. ProDOS `.po` images are 5.25" disks, so use the device's
   **Disk II (5.25") mode**.
2. **ADTPro** over a serial/audio link - write the `.po` to a real 5.25"
   floppy from a modern computer.

**Program disk + data disk together.** The data disk
(`swiftii-data.po`, with `TESTS/` + the full `SAMPLES/`) is a separate
drive-2 volume. To use it alongside a program disk you need two drives -
a second physical drive, or a **Floppy Emu in dual-disk mode** (program
disk = drive 1, data disk = drive 2). A single drive is enough for
everyday use: each program disk carries its own `SAMPLES/`.

A full user-facing walkthrough is in
[the tutorial](../using/TUTORIAL.md) ("Running on real hardware").

---

## Troubleshooting

### `cl65: command not found`

Homebrew's bin directory isn't on your `PATH`. After install:

```sh
export PATH="/opt/homebrew/bin:$PATH"      # Apple Silicon
export PATH="/usr/local/bin:$PATH"          # Intel
```

### `Error: Configuration error: Memory area overflow`

The most common cc65 error in this project. The linker is telling
you that some segment doesn't fit in the memory area you assigned to
it. Look at `build/apple2/swiftii.map` to see which segment is too
big and adjust either the segment contents (smaller code) or the
memory layout (`.cfg` file, with caution).

### `make apple2` succeeds but the emulator boots to BASIC

The disk-image build probably succeeded but ProDOS isn't finding a
`*.SYSTEM` file to auto-launch. Check that the disk build placed all
the expected files at the volume root:

```sh
java -jar "$APPLECOMMANDER_JAR" -ls build/disk/swiftii-iip-lite-repl.po
```

You should see `SWIFTII.SYSTEM` (boot selector) followed by that disk's one
interpreter - `SWIFTIIP.SYSTEM` (iip-lite), `SWIFTSAT.SYSTEM` (iip-sat),
`SWIFTIIE.SYSTEM` (iie-lite), or `SWIFTAUX.SYSTEM` (iie-aux). ProDOS 2.4.3
picks the first `*.SYSTEM` file in directory order, which is the boot selector -
it then chains the interpreter present on the volume.

### `make size` reports a budget violation

Look at `build/apple2/swiftii.map`. The linker map shows the size of
each segment. If the offending segment is one you just changed, your
change made the binary bigger than its budget. Options:

1. Make the change smaller (often: replace a function with a table).
2. Move some functions from a tight memory area (language card $E000)
   to a roomier one ($D000 bank-switched, or main RAM).
3. Genuinely need more space? Discuss before raising the budget in
   `CONSTRAINTS.md` - adjustments cascade.

### Emulator launches but screen is garbled

Mariani occasionally gets confused about the disk image format. Make
sure the file extension is `.po` (ProDOS order), which is what our
build emits.

### Tests pass on host but fail under `make sim`

A genuine 6502/host divergence - most often signedness or width
assumptions. Check:

- Are you using `int` somewhere? It's 32-bit on host, 16-bit on
  cc65.
- Are you treating `char` as signed? It's unsigned by default on
  cc65, signed (typically) on the host.
- Are you treating a pointer as if it were 64-bit?
- Are you using a C99 feature cc65 doesn't fully implement
  (designated initializers in unusual contexts, compound literals,
  VLAs)?

Add a note to `LESSONS.md` once you've fixed it.

### `cc65` produces correct code but it's huge

Common causes and fixes:

- A stray `printf` somewhere is pulling in the formatting runtime.
  Search for it and replace with `print_*` helpers.
- `enum` declarations using default `int` width - change to
  `unsigned char` typedefs.
- Recursion or function-pointer-heavy code defeating cc65's static
  stack analysis. Refactor.
- Forgetting `-O -Cl` flags on the build.

---

## CI

There's no formal CI yet (this is a hobby project). The closest thing
is `make ci`, which runs `clean test sim integration repl-test
repl-test-iie apple2-all apple2-familyb boot-launcher size disks
disks-familyb check-readme` and exits nonzero on any failure. `repl-test`
uses the host superset binary; `repl-test-iie` uses the no-`WITH_SWB` //e-like
host binary for //e REPL features and Family-A dialect rejection. Run it before
pushing.

If you want a real CI, GitHub Actions can run all of the above on
Ubuntu - cc65 is in the Ubuntu repos (`apt install cc65`). The
emulator step would be skipped on CI; everything else is fully
automatable.
