# datadisk/tests/

Self-checking SwiftII programs that exercise the **target** — cc65 codegen,
the 64 K layout, real ProDOS MLI, the Family-B compiler→runner path — ground the
unit suite doesn't cover. They are **tiered by the capability each test needs**,
one subdirectory per tier under this one walkable tree; [`../README.md`](../README.md)
has the tier → on-disk-directory mapping.

Because they are ordinary self-checking `.swift` programs, `make ondisk-host`
also runs the `core/` + `fbtests/` suites through the **host** build for fast,
emulator-free regression coverage of the same compiler + VM logic (it's in
`ci`). That complements — does not replace — the on-target run below: only the
emulator reaches the target-specific ground (cc65 codegen, real MLI, the paged
tiers).

- [`core/`](core) — **general**: any REPL (lite or extras) or the runner.
- [`xtests/`](xtests) — **extras**: a SWIFTSAT / SWIFTAUX extras REPL.
- [`fbtests/`](fbtests) — **Family B**: the compiler-runner only.
- [`errtests/`](errtests) — error-message **demos** (deliberately fail; not
  self-checking, so excluded from the automated sweep).

## How they report

Each test prints one `FAIL …` line per wrong result and ends with a tally, so
on a 40-column screen you only read the **last line** — `fail 0` means every
check passed; a `FAIL got X want Y` line names what diverged. The pattern is a
tiny in-file harness:

```swift
var npass = 0
var nfail = 0
func chk(_ got: Int, _ want: Int) {
  if got == want { npass = npass + 1 }
  else { nfail = nfail + 1; print("FAIL got \(got) want \(want)") }
}
```

`chk` compares `Int`s and `chkb` compares `Bool`s — both immediates, compared
by value. (Strings are **not** compared with `==`; see `core/tstring.swift`.)

## Running them

**On target / emulator:** boot a disk whose binary supports the tier, mount
this data disk in drive 2, open the file browser (`3` FILES), enter
`TESTS/<TIER>/`, highlight a test, and run it — **RET** on a REPL disk, **[X]**
on a Family B compiler disk. The `*-2disk` run targets auto-mount the data
disk:

```
# Mariani (machine is a GUI setting)
make run-mari-iip-2disk        # II+ boot disk + data disk      — CORE
make run-mari-aux-2disk        # //e boot disk + data disk      — CORE (+ XTESTS on aux)
make run-mari-compiler-2disk   # II+ compiler-runner + data disk — CORE + XTESTS + FBTESTS

# izapple2 (machine chosen per target)
make run-iz-iip-2disk             # II+ lite (SWIFTIIP)      — CORE
make run-iz-sat-2disk             # II+ Saturn (SWIFTSAT)    — CORE + XTESTS
make run-iz-iie-2disk             # //e aux (SWIFTAUX)       — CORE + XTESTS
make run-iz-compiler-2disk        # II+ compiler-runner      — CORE + XTESTS + FBTESTS
make run-iz-compiler-iie-2disk    # //e compiler-runner      — CORE + XTESTS + FBTESTS
```

Or sweep every test applicable to the booted disk back-to-back from the
launcher's **Run tests (data disk)** menu option, which chains
`/SWIFTII.DATA/TESTRUN.SYSTEM` (the on-target harness; design doc
[018](../../docs/contributing/design/018-on-target-test-harness.md)).
(`DATA_DISK=build/disk/swiftii-data.po make run-iz-…` mounts the data disk by
hand on any run target.)

**On the host** (the host build defines `WITH_SWB`, so it has every tier's
surface):

```
make host
for f in datadisk/tests/*/*.swift; do ./build/host/swiftii_host "$f"; done
```

`fbtests/` write files to the cwd — run those from a scratch dir
([`fbtests/`](fbtests)); `errtests/` are *meant* to fail, so skip them on the
host.

## Constraints when adding a test

- **Source ≤ 2048 B** — the launcher stages a Family-A file into a 2 KB buffer;
  `build_data_po.sh` fails the build on a larger file. (The Family B Compiler
  streams source with no such cap, but keep `fbtests/` under 2 KB anyway if you
  also want to run them on the host, whose runner is a Family A interpreter.)
- **Compiled bytecode ≤ 1024 B** — the interpreter's `FILE_BC_SIZE` buffer. The
  host build uses the same cap, so if a test runs on the host it fits the target.
- **≤ 32 globals** — each distinct top-level `for`/`while` loop variable
  consumes a global slot. Wrap a single loop in a function (the loop var becomes
  a scoped local) to stay under the cap.
- **ProDOS filenames ≤ 15 chars** — including the `.SWIFT` extension, so the
  base name is ≤ 9 characters.
