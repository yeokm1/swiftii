# releases/

Prebuilt, ready-to-use SwiftII disk images (`.po`) for download — so you
don't have to install the toolchain and build them yourself.

Images are organised by version, one folder per release:

```
releases/
  v1.0.0/   ← the eight .po images for that version
  v1.0.1/   ← the nine .po images for that version
  v1.0.2/   ← the nine .po images for that version
```

`make release` builds the disks and stages them under
`releases/v<version>/` (the version comes from
[`src/common/version.h`](../src/common/version.h)). These per-version
folders are committed to the repo — the `*.po` gitignore is exempted for
`releases/**/*.po` — and the same images are attached to each tagged
**[GitHub Release](https://github.com/yeokm1/swiftii/releases)** (in the
release *Assets*).

## Release history

| Version | Date | Highlights |
|---------|------|------------|
| **v1.0.2** | 2026-06-30 | Disable the ProDOS `/RAM` disk on 128 K //e |
| **v1.0.1** | 2026-06-27 | See notes below |
| **v1.0.0** | 2026-06-25 | Initial public release |

### v1.0.2 — 2026-06-30

- Disable the ProDOS `/RAM` disk on a 128 K //e. On those machines ProDOS
  publishes a `/RAM` volume backed by the same auxiliary RAM the //e extras
  builds reuse (SWIFTAUX's copy-down park and the aux-paged Family B
  compiler/runner). The boot launcher now removes `/RAM` from the on-line
  device list at startup, so it no longer appears in the volume picker and the
  aux RAM is no longer shadowed by a live device. Machines without a 64 KB aux
  card (the II+ and non-aux //e) are unaffected.

### v1.0.1 — 2026-06-27

- Short-circuit `&&` / `||` logical operators.
- New //e-native Family B compiler disk `swiftii-iie-compiler.po`: firmware
  80-column + native lowercase for a //e **without** the 64 KB aux card (which
  previously fell back to the II+ disk). The aux-paged //e disk is renamed
  `swiftii-iie-aux-compiler.po`; the set grows from 8 to 9 disks.
- The //e aux compiler disk now shows its own `SwiftII Compiler //e aux` banner.
- Fixed the on-disk "Run tests" sweep filling the data disk: the Runner now
  deletes each test's generated `.swb` after running it (in the test-sweep
  auto-advance), so the outputs no longer pile up and exhaust the data disk's
  free space partway through a full Family B sweep.
- Restored the SWIFTSAT REPL blinking cursor without reintroducing the SAT
  acceptance crash: only the one-key cursor wait is banked into XLC; the line
  editor and compiler entry stay in MAIN.

### v1.0.0 — 2026-06-25

- Initial public release: the SwiftII interpreter (immediate **REPL** + **file**
  mode) and the Pascal-style **Family B** compiler/runner toolchain, with the
  in-launcher editor, graphics / sound / memory built-ins, and 80-column support.
  Eight-disk distribution spanning the Apple ][ / ][+, II+ + Saturn 128 K, //e,
  and //e + 64 KB aux machines.

## The nine-disk set

Staged release images carry a `-v<version>` suffix (e.g.
`swiftii-iip-lite-repl-v1.0.2.po`) so a single `.po` downloaded on its own
from a GitHub Release still names its version. The tables below list the
canonical stem; append `-v<version>` for the actual filename.

| File | Machine | Use |
|------|---------|-----|
| `swiftii-iip-lite-repl.po` | any ][ / ][+ | REPL, core language |
| `swiftii-iip-sat-repl.po` | ][+ with Saturn 128K | REPL + graphics / memory / speaker click + 80-col (Videx) |
| `swiftii-iie-lite-repl.po` | any //e | REPL, core language |
| `swiftii-iie-aux-repl.po` | //e with 64K aux | REPL + graphics / memory / speaker click + 80-col |
| `swiftii-data.po` | any (drive 2) | non-boot data disk: full `SAMPLES/` + `TESTS/` |
| `swiftii-iip-compiler.po` | ][+ | Family B compiler + runner (Tier 1, II+) |
| `swiftii-iie-compiler.po` | any //e | Family B (Tier 1, //e-native: firmware 80-col, no aux card needed) |
| `swiftii-iie-aux-compiler.po` | //e with 64K aux | Family B (Tier 3, aux-paged) |
| `swiftii-iip-sat-compiler.po` | ][+ Saturn | Family B (Tier 2, Saturn-paged) |

## v1.0.2 disk contents snapshot

Free-space figures are read from the freshly staged `.po` images
(AppleCommander disk listing), captured 2026-06-30 — `make disks check-readme`
verifies each disk's `README.TXT` and launcher banner, not its free bytes, so
refresh these numbers from the build when the disk contents change. Every
bootable disk carries `PRODOS`, `SWIFTII.SYSTEM` (launcher), `DEBUG.SYSTEM`, and
`README.TXT` unless noted.

| Image | Free bytes | Additional root payload |
|-------|-----------:|-------------------------|
| `swiftii-iip-lite-repl.po` | 29,696 | `SWIFTIIP.SYSTEM`, `SAMPLES/` |
| `swiftii-iip-sat-repl.po` | 12,288 | `SWIFTSAT.SYSTEM`, `SAMPLES/`, `XSAMPLES/` (extras staged-source samples only) |
| `swiftii-iie-lite-repl.po` | 31,232 | `SWIFTIIE.SYSTEM`, `SAMPLES/` |
| `swiftii-iie-aux-repl.po` | 14,336 | `SWIFTAUX.SYSTEM`, `SAMPLES/`, `XSAMPLES/` (extras staged-source samples only) |
| `swiftii-data.po` | 19,968 | non-boot data disk: `SAMPLES/`, full `XSAMPLES/`, `TESTS/`, `TESTRUN.SYSTEM` |
| `swiftii-iip-compiler.po` | 5,120 | `COMPILER.SYSTEM`, `RUNNER.SYSTEM`, minimal `SAMPLES/`, `XSAMPLES/XSNAKE.SWIFT` |
| `swiftii-iie-compiler.po` | 8,704 | `COMPILER.SYSTEM`, `RUNNER.SYSTEM`, minimal `SAMPLES/`, `XSAMPLES/XSNAKE.SWIFT` |
| `swiftii-iie-aux-compiler.po` | 7,168 | `COMPILER.SYSTEM`, `RUNNER.SYSTEM`, minimal `SAMPLES/`, `XSAMPLES/XSNAKE.SWIFT` |
| `swiftii-iip-sat-compiler.po` | 3,072 | `COMPILER.SYSTEM`, `RUNNER.SYSTEM`, minimal `SAMPLES/`, `XSAMPLES/XSNAKE.SWIFT` |

Program-disk `SAMPLES/` is the portable set (`arrays`, `fib`, `fizzbuzz`,
`functions`, `greet`, `optionals`, `strings`) except on compiler disks, where
it is trimmed to `functions` and `greet` to leave room for generated `.swb`
files. The full data disk adds the extras demos (`xcolors`, `xgraphics`,
`xsnake`, `xspeaker`, `xvtab`), Family-B demos (`xdice`, `xwide`), oversize compiler
showcases (`xbig`, `xgrdemo`, `xfuncs`), and the on-target `TESTS/` tree (`CORE/`,
`XTESTS/`, `FBTESTS/`, `ERRTESTS/`).

## Using them

Each `.po` is a standard **140 KB ProDOS 5.25" image**. Write one to a real
floppy with **ADTPro**, or copy it to a **floppy emulator** (e.g. BMOW
Floppy Emu in Disk II mode) and boot. To run a program disk together with
the data disk, you need two drives — a second physical drive, or a Floppy
Emu in **dual-disk mode** (program disk = drive 1, data disk = drive 2).

Full step-by-step instructions are in the
[user tutorial](../docs/using/TUTORIAL.md) ("Running on real hardware").

## (Re)building a version folder

```sh
make release    # builds the nine disks and stages them in releases/v<version>/
```

The version comes from [`src/common/version.h`](../src/common/version.h) —
bump that one line before cutting a new release, then `make release` and
commit the new `releases/v<version>/` folder.

> **Run the acceptance suite yourself before tagging a release.**
> `make release` only *builds* the disks — it does not boot or verify
> them, and the acceptance harness is **not** part of the release process
> (it boots every image and is far too slow for that). Run it as a
> separate manual step, ideally with the live browser window open so you
> can watch each disk boot:
>
> ```sh
> make acceptance ARGS=--window
> ```
>
> To verify the **exact images you just staged** (rather than a fresh
> build), point the harness at this version folder:
>
> ```sh
> make acceptance RELEASE=releases/v1.0.2 ARGS=--window
> ```
>
> The images are copied into `build/acceptance/` and run from there, so the
> staged release files are never modified. See
> [BUILDING.md](../docs/contributing/BUILDING.md) ("The automated
> acceptance harness") for the other invocation modes.
