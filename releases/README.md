# releases/

Prebuilt, ready-to-use SwiftII disk images (`.po`) for download — so you
don't have to install the toolchain and build them yourself.

Images are organised by version, one folder per release:

```
releases/
  v1.0.0/   ← the eight .po images for that version
```

`make release` builds the eight disks and stages them under
`releases/v<version>/` (the version comes from
[`src/common/version.h`](../src/common/version.h)). These per-version
folders are committed to the repo — the `*.po` gitignore is exempted for
`releases/**/*.po` — and the same images are attached to each tagged
**[GitHub Release](https://github.com/yeokm1/swiftii/releases)** (in the
release *Assets*).

## The eight-disk set

Staged release images carry a `-v<version>` suffix (e.g.
`swiftii-iip-lite-repl-v1.0.0.po`) so a single `.po` downloaded on its own
from a GitHub Release still names its version. The tables below list the
canonical stem; append `-v<version>` for the actual filename.

| File | Machine | Use |
|------|---------|-----|
| `swiftii-iip-lite-repl.po` | any ][ / ][+ | REPL, core language |
| `swiftii-iip-sat-repl.po` | ][+ with Saturn 128K | REPL + graphics / memory / speaker click + 80-col (Videx) |
| `swiftii-iie-lite-repl.po` | any //e | REPL, core language |
| `swiftii-iie-aux-repl.po` | //e with 64K aux | REPL + graphics / memory / speaker click + 80-col |
| `swiftii-data.po` | any (drive 2) | non-boot data disk: full `SAMPLES/` + `TESTS/` |
| `swiftii-iip-compiler.po` | ][+ | Family B compiler + runner (Tier 1) |
| `swiftii-iip-sat-compiler.po` | ][+ Saturn | Family B (Tier 2, Saturn-paged) |
| `swiftii-iie-compiler.po` | //e aux | Family B (Tier 3, aux-paged) |

## v1.0.0 disk contents snapshot

Current free-space figures are from `make disks check-readme` on
2026-06-24. Every bootable disk carries `PRODOS`, `SWIFTII.SYSTEM`
(launcher), `DEBUG.SYSTEM`, and `README.TXT` unless noted.

| Image | Free bytes | Additional root payload |
|-------|-----------:|-------------------------|
| `swiftii-iip-lite-repl.po` | 30,208 | `SWIFTIIP.SYSTEM`, `SAMPLES/` |
| `swiftii-iip-sat-repl.po` | 12,800 | `SWIFTSAT.SYSTEM`, `SAMPLES/`, `XSAMPLES/` (extras staged-source samples only) |
| `swiftii-iie-lite-repl.po` | 31,232 | `SWIFTIIE.SYSTEM`, `SAMPLES/` |
| `swiftii-iie-aux-repl.po` | 14,336 | `SWIFTAUX.SYSTEM`, `SAMPLES/`, `XSAMPLES/` (extras staged-source samples only) |
| `swiftii-data.po` | 22,528 | non-boot data disk: `SAMPLES/`, full `XSAMPLES/`, `TESTS/`, `TESTRUN.SYSTEM` |
| `swiftii-iip-compiler.po` | 5,632 | `COMPILER.SYSTEM`, `RUNNER.SYSTEM`, minimal `SAMPLES/`, `XSAMPLES/XSNAKE.SWIFT` |
| `swiftii-iip-sat-compiler.po` | 3,584 | `COMPILER.SYSTEM`, `RUNNER.SYSTEM`, minimal `SAMPLES/`, `XSAMPLES/XSNAKE.SWIFT` |
| `swiftii-iie-compiler.po` | 7,168 | `COMPILER.SYSTEM`, `RUNNER.SYSTEM`, minimal `SAMPLES/`, `XSAMPLES/XSNAKE.SWIFT` |

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
make release    # builds the eight disks and stages them in releases/v<version>/
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
> make acceptance RELEASE=releases/v1.0.0 ARGS=--window
> ```
>
> The images are copied into `build/acceptance/` and run from there, so the
> staged release files are never modified. See
> [BUILDING.md](../docs/contributing/BUILDING.md) ("The automated
> acceptance harness") for the other invocation modes.
