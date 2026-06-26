# TESTING.md

How SwiftII is tested. The short version: **most tests run on the
host in milliseconds, a smaller set runs on a 6502 simulator in
seconds, almost nothing requires the emulator.**

If you're adding a feature, write a test for it. If you're fixing a
bug, write a regression test before fixing it.

For the *target*, the self-checking suites are automated end-to-end by **`make
acceptance`** - the harness in [`tools/host/acceptance/`](../../tools/host/acceptance)
boots izapple2's `headless` frontend on every hardware config, injects
keystrokes, scrapes the screen, reads the Family B `TESTLOG`, and reports
pass/fail across the matrix (embedded ROMs - nothing to source). It is the
automated counterpart to the manual playbooks below.

Sibling docs cover the on-target passes: the feature walk is split
into [`TESTING-emulators.md`](TESTING-emulators.md) (the emulator
pre-flight, all nine disks - now mostly automated by `make acceptance`) and
[`TESTING-iiplus.md`](TESTING-iiplus.md)
(the physical II+ acceptance gate), plus the exhaustive key-by-key matrix in
[`TESTING-keyboard.md`](TESTING-keyboard.md) (every
shortcut in every UI mode - launcher, file selector, editor, REPL, Debug,
test runner, 80-col). The editor's key logic and the //e history ring are
pure functions and so are host-tested (`tests/editor/keymap_test.c`,
`tests/platform/histring_test.c`); the launcher / browser / Debug / test-runner
key routing reads the keyboard inline in cc65 loops and is **manual by design** - extracting it was measured and declined on cost-benefit (design doc 019), so
the keyboard matrix is its permanent test of record and a required release-gate
step, not a stopgap.

---

## The five test layers

```
tests/
  unit/          host-side C unit tests (clang)
  sim/           bytecode tests on py65 (6502 sim, no Apple II)
  integration/   .swift programs run end-to-end in file mode
  repl/          scripted REPL session tests
  ondisk/        host-runs the on-disk datadisk/tests/ suites (core + fbtests)
```

Each layer answers a different question. They are complementary, not
redundant.

### `tests/unit/` - host-side C unit tests

What: small C programs that link against parts of the interpreter and
test them directly.

When to use: testing a function or module in isolation. Lexer token
recognition, heap allocator behavior, value type predicates,
compiler-internal helpers, individual opcode implementations.

Why: it's the fastest feedback loop in the project - sub-second from
save to test result. It uses `clang` (host compiler) with sanitizers
enabled, so memory bugs surface immediately rather than corrupting an
emulator's RAM.

Layout:

```
tests/unit/
  lexer_test.c       runs the lexer on canned inputs, checks tokens
  compiler_test.c    compiles snippets, checks emitted bytecode
  heap_test.c        allocates, frees, checks fragmentation
  value_test.c       tests tagged-value predicates and conversions
  vm_test.c          runs the VM C-fallback dispatch on tiny progs
  runner.c           main(), discovers and runs all test functions
```

Run with `make test`. Each test function returns 0 for pass, nonzero
for fail; the runner aggregates results.

### `tests/sim/` - 6502 simulator tests

What: small bytecode programs (or compiled C functions) executed on
py65, a bare 6502 simulator with a configurable memory map.

When to use: catching divergences between host and target codegen.
Verifying that hand-written assembly produces the right results.
Testing the actual VM dispatch loop end-to-end without booting an
emulator.

Why: clang on the host and cc65 on the target produce different code
with different size assumptions. py65 lets us run the *target*
binary on a 6502 in microseconds, with full register and memory
inspection.

Layout:

```
tests/sim/
  smoke_test.py       minimal py65 harness smoke test
  dispatch_test.py    runs canned bytecode, checks final stack/heap
  arith_test.py       multiply/divide correctness across ranges
  refcount_test.py    allocation patterns + leak detection
  swiftaux_copydown_test.py
                      SWIFTAUX aux copy-down dispatcher coverage
  helpers.py          shared py65 setup
  runner.py           test discovery / runner
```

Run with `make sim`.

### `tests/integration/` - Swift programs in file mode

What: real `.swift` files plus an `.expected` file showing what should
come out of `print` (and `stderr`).

When to use: verifying user-visible language behavior. Most language
features should have at least one integration test.

Why: integration tests are the closest thing to "what will users
actually see." They catch regressions across the full pipeline that
unit tests can miss.

Layout:

```
tests/integration/
  001_arithmetic.swift
  001_arithmetic.expected
  002_fizzbuzz.swift
  002_fizzbuzz.expected
  003_strings.swift
  003_strings.expected
  ...
  026_system.swift
  026_system.expected
  027_terminator_expr.swift
  027_terminator_expr.expected
  028_booleans.swift
  028_booleans.expected
  800_fileio.swift
  800_fileio.expected
  ...
  runner.sh                 runs each, diffs output
```

The runner runs each `.swift` file through the host build of SwiftII
(file mode), captures stdout, and diffs against the `.expected` file.
A nonzero exit code or a diff means the test fails.

Run with `make integration` (or as part of `make ci`).

### `tests/repl/` - scripted REPL sessions

What: `.repl` files that look like a REPL transcript - alternating
input lines and expected output lines.

When to use: anything REPL-specific. Multi-line input handling,
top-level expression printing, function redefinition, error recovery,
meta-commands.

Why: file-mode tests don't exercise the REPL's incremental compiler,
its scratch bytecode area, its session-state preservation, or its
error recovery. These are real and have their own failure modes.

Error messages: `017_errors.{repl,expected}` is the end-to-end surface
check - it exercises a representative spread of compile-error messages
(undeclared name, expected `')'`/expression/name/`'{'`, type mismatch, unknown
member, missing return, use-positional-args) and the runtime-error path
(div-by-zero, out-of-bounds, nil-unwrap), then a final successful line
proving recovery, confirming the REPL renders them as `compile error: …`
/ `runtime error` and keeps the session alive. The *exhaustive* per-message
coverage lives host-side in `tests/unit/error_paths_test.c`: one assertion
per distinct compiler, lexer and VM error string (including the
resource-limit messages - too many locals/funcs/params/args/elements,
globals full, loops too deep, etc. - that a single REPL line cannot reach),
plus the runtime `SE_*` codes. That file also documents the few messages
deliberately left untested (build-gated, dead, or out-of-memory guards) and
why. The matching on-**target** display demos
(`compile error: line N: …` with source echo; the Runner's `runtime error`)
live on the data disk under `TESTS/ERRTESTS/` (`datadisk/tests/errtests/`) - run
them with `[X]` on a Family B compiler disk; they deliberately fail, so they are
not self-checking.

Layout:

```
tests/repl/
  001_demo.repl
  002_bare_expression.repl
  003_metacmds.repl
  ...
  017_errors.repl
  runner.sh

tests/repl-iie/
  001_func_redef.repl       // //e-only REPL behavior
```

Format example:

```
> let x = 5
> let y = 10
> print(x + y)
15
> :quit
```

Lines starting with `> ` are input; other lines are expected output
from the REPL (excluding the prompt itself, which the runner
matches automatically). `:quit` ends the session.

Run with `make repl-test` (or as part of `make ci`).

### `tests/ondisk/` - the on-disk suites, run on the host

What: a thin runner (`tests/ondisk/runner.sh`) that feeds the **self-checking
on-disk tests** - `datadisk/tests/core/` + `datadisk/tests/fbtests/` - through
the host build and asserts each ends in `... fail 0`.

When to use: it picks these up automatically, so you rarely invoke it directly;
just keep on-disk tests self-checking. It exists because those tests are the
**target** system of record (they run on the emulator via `make acceptance`,
below) but are ordinary self-checking `.swift` programs, so the host can run the
same compiler + VM logic in milliseconds. That makes a regression like the
if/else branch local-scoping bug (guarded by `core/tscope.swift` +
`fbtests/tscopefb.swift`) fail in `make ci` instead of only on the emulator.

Why a separate layer and not `integration/`: these tests are written in the
on-disk `chk`/`fail 0` self-check style and shipped on the data disk (so they
also run on real hardware), whereas `integration/` fixtures are host-only and
diffed against `.expected`. The file-I/O fbtests write relative paths, so the
runner runs each from a throwaway CWD.

It complements - does not replace - `make acceptance`: only the emulator reaches
the target-specific ground (cc65 codegen, real ProDOS MLI, the paged tiers).

Run with `make ondisk-host` (or as part of `make ci`).

---

## What to test for new features

When adding a feature, ask: **how could this break?** Each plausible
failure mode is a test.

For a new opcode:
- Unit test: compile a snippet that uses it, check the emitted bytes.
- Sim test: run the bytecode, check the result.
- Integration test: a Swift program that exercises the user-facing
  feature.

For a new language construct (e.g. `if let`):
- Integration test: typical use, edge cases (nested, trailing,
  variable shadowing).
- REPL test: multi-line entry of a block containing the construct.
- Unit test: parser produces the right bytecode shape.

For a new builtin (e.g. `readLine`):
- Integration test with input redirection.
- REPL test with simulated keyboard input.
- Unit test for the C implementation.

For a refactor:
- All existing tests should still pass.
- If the refactor is meant to improve some property (size, speed),
  add a test or check that observes it.

## Test naming and ordering

Tests are numbered (`001_`, `002_`, ...) so they have a stable order
and so it's obvious where to insert a new one. The numbers carry no
semantic meaning beyond rough chronology and grouping:

- `001-099`: smoke tests, basic language features
- `100-199`: control flow
- `200-299`: functions and scoping
- `300-399`: optionals
- `400-499`: collections
- `500-599`: strings
- `600-699`: REPL-specific behaviors
- `700-799`: reserved (unused)
- `800-899`: open
- `900+`:    bug regressions (one per fixed bug, named after the bug)

When a number range fills up, just keep going past 099. The numbers
are for ordering, not for capacity.

---

## Running everything

```sh
make ci
```

A single `make ci` invocation runs, in order:

1. `make clean`
2. `make test`         (host unit tests)
3. `make sim`          (6502 simulator)
4. `make integration`  (file-mode end-to-end; respects `// requires:`)
5. `make ondisk-host`  (the on-disk core + fbtests suites, run on the host)
6. `make repl-test`    (host-superset REPL sessions; core plus host-testable extras)
7. `make repl-test-iie` (//e/no-`WITH_SWB` REPL sessions and Family-A dialect rejection)
8. `make apple2-all`   (all four Family A interpreters)
9. `make apple2-familyb` (all Family B Compiler/Runner builds: 3 tiers, 4 disks — II+, //e non-aux, //e aux, Saturn)
10. `make boot-launcher`
11. `make size`        (memory and file-size budget check)
12. `make disks` and `make disks-familyb`
13. `make check-readme`

This is the push gate for any change that touches the lexer, compiler, VM,
heap, builtin dispatch, platform code, disk layout, or documentation shipped
on disk.

---

## Emulator hardware configurations (`make run-*`)

Interactive verification across real-ish machine configs - these `make run-*`
targets launch izapple2sdl (or Mariani) in a window for human eyeballs. For the
*self-checking* suites the same configs are driven hands-free by **`make
acceptance`** (see the top of this doc and
[`tools/host/acceptance/`](../../tools/host/acceptance)); the `make run-*` targets below
are for watching a config and for the feel/visual checks the harness can't make.
`make run-configs` prints a terse terminal version; this is the canonical list
with expected results. Pass the emulator as needed, e.g. `make run-iz-sat-2disk
IZAPPLE2=./izapple2sdl_mac_arm64`.

The **standard matrix** targets all mount the DATA disk (samples + `TESTS/`) in
drive 2 so you can run the test suite once the config boots - that's why they
end in `-2disk` - `run-iz-*-2disk` (izapple2) and `run-mari-*-2disk` (Mariani). (The
demo `SAMPLES/` also ship on each program disk now, so a single-disk boot
already has programs to run; the data disk adds the `TESTS/`.) The
**edge / negative** cases boot the system disk alone: they test boot failure or
binary selection, not program execution. Each row's single-disk form (drop the
`-2disk` suffix) still exists for a quick boot without the data disk.

**Which emulator does what:** izapple2 is CLI-configured with embedded ROMs
(easiest); it **runs SWIFTSAT on its Saturn** (REPL confirmed working) and
models the //e as always-128K. Mariani has **no machine CLI** (set the
model/Saturn/aux in its GUI) and covers the plain //e 64K/no-aux row. A
basic-80-col //e and RAMWorks III are real-hardware-only for now.

An II+ 80-column path via the **Videx Videoterm** (slot 3) is
**REPL/program only**: the REPL path is `SWIFTSAT`, so it requires Saturn plus
Videx (opt-in `text80()`/`text()`/`htab`). The Family B `RUNNER` can also drive
a Videoterm for program output on the flat II+ tier. The lite REPL / launcher /
editor stay 40-column (RAM-walled). Test the REPL path with
`make run-iz-videx-2disk` (izapple2, II+ Saturn + Videx, output-routing smoke
only - its Videoterm ROM diverges) and, authoritatively, a real II+ + Saturn +
Videoterm (config #3 below + a Videoterm; `text80()` -> 80-col, `text()` reverts,
the `xwide.swift` sample, output persistence).

### Standard matrix

| #  | Machine config                       | Target(s) (data disk in drive 2)             | Selects   | Expected |
|----|--------------------------------------|----------------------------------------------|-----------|----------|
| 1  | II+ 16K LC (64K)                     | `run-iz-iip-2disk` · `run-mari-iip-2disk`    | SWIFTIIP  | `SwiftII ][+`, 40-col REPL |
| 3  | II+ Saturn 128K                      | `run-iz-sat-2disk` · `run-iz-videx-2disk` (adds Videx) · `run-mari-sat-2disk` | SWIFTSAT  | `SwiftII ][+ Saturn`; REPL + XLC builtins work (40-col; +`text80()` 80-col on Videx) |
| 5  | //e 64K (no aux)                     | `run-mari-iie-2disk`                         | SWIFTIIE  | `SwiftII //e`, 40-col, native lowercase |
| 6  | //e + basic 80-col (1K)              | _(real hardware only)_                       | SWIFTIIE | as #5 - boot the iie-lite disk |
| 7  | //e + extended 80-col (64K→128K)     | `run-iz-iienh-2disk` · `run-mari-aux-2disk`  | SWIFTAUX  | `SwiftII //e aux`; 80-col + full extras |
| 8  | //e + RAMWorks III                   | _(real hardware only)_                       | SWIFTAUX | as #7 (large aux) |
| 9  | //e enhanced (65C02, 128K)           | `run-iz-iienh-2disk`                          | SWIFTAUX  | as #7 - 6502 code runs on a 65C02 |

REPL disk set: the **disk picks the interpreter** (one per image), not a HW probe -
so the izapple2 //e profiles boot the disk that forces the binary under test
(`run-iz-iie` → iie-lite/SWIFTIIE on izapple2's 128K //e, `run-iz-iienh` →
iie-aux/SWIFTAUX). For Mariani, also set the model/Saturn/aux in its GUI (it
has no machine CLI) to match the disk; `run-mari-iie` is the plain 64K/no-aux
//e smoke. `run-mari-iip` / `run-mari-sat` / `run-mari-iie` / `run-mari-aux`
are the single-disk equivalents and print the exact GUI setting to select.

Note: izapple2 runs SWIFTSAT on its Saturn, and keyboard echo works in 40-col.

### Edge / negative cases

| #  | Machine config                            | Target            | Selects  | Expected |
|----|-------------------------------------------|-------------------|----------|----------|
| 0  | Original Apple ][ + 16K language card     | `run-iz-ii`       | SWIFTIIP | boots the II+ lite disk; use `C600G` from the monitor prompt |
| 10 | II+ **48K, no language card**             | `run-iz-iip48`    | (none)   | **does not boot** - ProDOS needs 64K; a clean failure is the pass |
| 11 | II+ Saturn in a **non-zero slot** (4)     | `run-iz-sat-s4`   | SWIFTSAT | launcher slot-scan finds it + selects SWIFTSAT (slot-conditional trampoline) |
| 12 | II+ + **non-Saturn RAM card** (`memexp`)  | `run-iz-memexp`   | SWIFTIIP | a generic RAM card must **not** false-trigger extras |

These edge / negative targets boot the **system disk alone** - they test boot
failure or which binary is selected, not program execution.

### Running the on-disk samples + test suite

The demo `SAMPLES/` ship on each program disk, so once a config boots you can
run them from the boot volume directly: press `2` (File selector) → enter
`SAMPLES/` → RETURN a file. The self-checking **test suite** lives on the
non-boot **data** disk (`make disk-data`) only, mounted in drive 2 by the
standard-matrix targets. The on-disk tests are **tiered by capability**, each
tier a subdirectory of one walkable `TESTS/` tree (design docs 017, 018):

| dir              | tier     | run on                                                |
|------------------|----------|-------------------------------------------------------|
| `TESTS/CORE/`    | general  | any REPL (`run-iz-iip-2disk`, `-sat-2disk`, …)         |
| `TESTS/XTESTS/`  | extras   | a SWIFTSAT/SWIFTAUX boot (`run-iz-sat-2disk` / `-iienh-2disk`) |
| `TESTS/FBTESTS/`  | Family B | a compiler-runner boot (`run-iz-compiler-2disk` / `-iie-2disk`) |
| `TESTS/ERRTESTS/`| demos    | a compiler-runner boot; deliberately fail, not self-checking |

The `CORE/` + `FBTESTS/` tiers (being self-checking `.swift`) also run on the
**host** in `make ci` via `make ondisk-host` - fast, emulator-free coverage of
the same compiler+VM logic. Only the emulator (`make acceptance`, below) reaches
the target-specific ground.

**The fully automated way - `make acceptance`.** The harness in
[`tools/host/acceptance/`](../../tools/host/acceptance) drives this whole sweep across
*every* emulator config without a human: it boots izapple2's `headless`
frontend, presses **Run tests**, advances each test, and reports pass/fail
(Family B verdicts read back from the on-disk `TESTLOG`; graphics/80-col
captured as PNGs). It also covers the user-facing **oversize showcases**
(`XSAMPLES/`) directly: the `samples` / `samples-sat` / `samples-iie` configs
`[X]`-run `xbig`/`xgrdemo`/`xfuncs` on each Family B tier and check the
checksums - the proof that `xbig`+`xgrdemo` run on all three tiers and `xfuncs`
is paged-only (rejected on flat II+). `make acceptance-build` once (Go), then
`make acceptance` (or `CONFIGS="iip sat"`). This is the automated outer loop for
everything below and for [`TESTING-emulators.md`](TESTING-emulators.md); see
[`tools/host/acceptance/README.md`](../../tools/host/acceptance/README.md).

**The fast manual way - Run tests on the boot menu (design doc 018).** With the
data disk in drive 2, choose **Run tests** at the launcher menu (its numbered
entry - option `4` on a REPL disk, `3` on a compiler disk) to **auto-run every
tier this disk's binary can run, back to back** -
no per-file browsing. It sweeps
`TESTS/CORE` (`+ TESTS/XTESTS` on a Saturn/aux extras disk), or `TESTS/FBTESTS`
on a compiler disk; `TESTS/ERRTESTS` is skipped (those fail on purpose). Read
each test's last line (`fail 0` = pass); how you advance to the next depends on
the disk:

- **REPL disks (CORE/XTESTS):** the test ends at the REPL prompt - press
  **Ctrl-D** (one keystroke: Ctrl-D exits the REPL to the launcher, which
  continues the sweep).
- **Compiler disks (FBTESTS):** the Runner **auto-advances after ~4 s** (no
  keypress) - glance at the result and it moves on.

When the queue empties it shows `Ran N tests`. The sweep is driven by
`TESTRUN.SYSTEM` (chained by the **Run tests** menu option, and again on each
reboot via a `TESTRUN` note on the data disk, so it survives the cold reboot).

**The manual way - one file at a time.** Press `2` (File selector) → select
the data disk → enter `TESTS/` then the tier's directory → RETURN (or `[X]` on
a compiler disk) → read the last line (`fail 0` = pass). The source lives under
`datadisk/tests/core/`, `datadisk/tests/xtests/`, `datadisk/tests/fbtests/`, and
`datadisk/tests/errtests/`, each with its own README. These run in the
on-device editor too, so keep every line within the 40-column budget
(`40 - gutter`, where the gutter is `digits(line_count) + 1`) - see
`datadisk/README.md`, "A note on line width".

> **Shipped (design doc 018):** the compiler-runner auto-*verify* follow-on -
> the Runner (built `-DWITH_TESTLOG`) writes a machine-checked `TESTLOG` so a
> `TESTS/FBTESTS` sweep both advances hands-off **and** reports pass/fail
> itself. `make acceptance` reads that `TESTLOG` back off the data-disk image
> with AppleCommander (emulator-verified: a full Family B sweep reports every
> test pass, `0 fail`). The REPL tiers keep the human-read + Ctrl-D gesture in the
> manual flow - a running REPL has no MLI to persist a verdict and the
> cold-reboot return erases the screen - so `make acceptance` instead scrapes
> each REPL test's on-screen `FAIL 0` tally for those tiers.

**File/directory I/O (Family B, design doc 017).** The file builtins
(`readFile`/`writeFile`/`appendFile`, `deleteFile`/`renameFile`/`fileExists`,
`createDirectory`/`deleteDirectory`/`listDirectory`) only exist on a Family B
compiler-runner. Three verification layers:

1. **Host C unit tests** - `tests/unit/file_io_test.c` covers the workers
   (`test_userfile_*` / `test_pf_*` / `test_userdir_*`), the compiler
   recognizers (`test_compile_*`), and full compile-then-`vm_run` round-trips
   (`test_run_write_then_read_roundtrip`, `test_run_append_and_delete`). Runs
   under `make test` / `make ci`.
2. **Host integration** - `tests/integration/800_fileio.swift` runs the whole
   compile→run pipeline through the `swiftii_host` binary, using hermetic
   `/tmp` paths, and is diffed against `800_fileio.expected` by `make
   integration` / `make ci`.
3. **On-target** - `datadisk/tests/fbtests/` on the data disk's `TESTS/FBTESTS/`:
   `tfileio.swift` (whole-file ops, 17 checks) and `tfiledir.swift` (directory
   ops, 22 checks), exhaustive over the nine builtins incl. the edge/failure
   paths (overwrite-truncate, missing→nil, non-empty-dir refusal, …). Boot a
   compiler disk with the data disk in drive 2 (`make run-iz-compiler-2disk` /
   `-iie-2disk`), switch to the data disk in the file selector, enter
   `TESTS/FBTESTS/`, and `[X]` each. This is the only layer that exercises real
   ProDOS MLI (the host uses stdio/POSIX); the data disk must be writable.
   Each prints `pass N fail 0`. **Verified on an emulated compiler-runner.**

### Verified vs owed

- **Verified on real hardware:** an **Apple II+ with a Saturn 128K card and a
  Videx Videoterm** is the only physical machine in the loop - it covers the
  II+ lite REPL, `SWIFTSAT` extras (incl. the `text80()` Videx 80-col path),
  and the Family B compiler/runner on iron.
- **Emulator-only (extensive, but never run on iron):** every other supported
  configuration - //e lite, //e aux (`SWIFTAUX`), the original ][, and the
  Family B disks on //e - is exercised by `make acceptance` and the
  standard-matrix `run-*` targets but has no physical-hardware pass.
- **Owed (no model exists):** #6 basic-80-col and #8 RAMWorks III are modelled
  by neither supported emulator, so they are untested everywhere until run on
  iron.

---

## What we don't test (and why)

- **Real Apple II hardware**: tested by hand at milestones, not in
  CI. Real-hardware-only bugs are rare given the simulator coverage,
  and we don't want a CI dependency on physical machines.
- **Emulator behavior** (beyond the acceptance harness): the self-checking
  suites are automated end-to-end by `make acceptance` (izapple2 `headless` -
  it injects keystrokes, scrapes the text screen, reads the Family B `TESTLOG`,
  and snapshots graphics). What stays human-eyeball-only is the *feel* -
  responsiveness, exact rendering, sound, and anything not self-checking - per
  the [`TESTING-emulators.md`](TESTING-emulators.md) playbook.
- **Performance**: there are no automated performance regression tests.
  Add some when there's enough functionality to make them stable.
- **Memory leaks across long REPL sessions**: there's a manual
  `:mem` command in the REPL that exposes the heap stats, but no
  automated long-running soak test.

---

## Debugging a failing test

1. **Host unit test failing**: it's just C. `lldb` works normally
   against `build/host/unit_tests`.
2. **Integration test failing**: run the failing `.swift` file
   manually with `./build/host/swiftii_host file.swift` and diff by
   eye. Add `--trace` to dump bytecode as it executes.
3. **Sim test failing**: py65 has a built-in monitor. The test
   harness exposes it on failure - you'll be dropped into a 6502
   debugger at the failure point.
4. **REPL test failing**: the runner reports the line where output
   diverged. Try the same inputs by hand in `./build/host/swiftii_host`.
5. **Apple2 build fails but host build passes**: 90% of the time
   it's `int` width, `char` signedness, pointer size, or a C99
   feature cc65 doesn't support. Check `LESSONS.md` for prior
   incidents.
