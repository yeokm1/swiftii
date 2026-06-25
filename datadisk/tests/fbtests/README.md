# datadisk/tests/fbtests/

The **Family B** tier: self-checking tests that need the **compiler-runner**
(mostly its surface the Family A REPLs reject). See [`../README.md`](../README.md)
for the `chk` harness, run targets, and constraints. Groups here:

- the **file/directory I/O** builtins (design doc 017) — `tfileio.swift`,
  `tfiledir.swift`;
- the **Phase 16 big-language** features (`WITH_BIGLANG` / `WITH_RANDOM`) —
  `tswitch.swift`, `tforarr.swift`, `trandom.swift`;
- the **value-stack local-scoping invariant** for the big-language body types
  (a `let`/`var` in a conditionally-taken `switch` case or `for-in`-array body
  must be popped on its own path) — `tscopefb.swift`, the Family-B companion to
  `core/tscope.swift` (which covers `if`/`else`/`while`/`for`-range);
- the **`wait(_ ms:)` delay** — `twait.swift`;
- the **`tone(_ halfPeriod:_ cycles:)` speaker tone** — `ttone.swift` (its
  middle section is *audible* — listen for five rising blips + a chirp on a
  real //e / II+; silent on host and on emulators without `$C030` audio); and
- the **`abs`/`sgn` math + `hasPrefix`/`hasSuffix` string methods** —
  `tmath.swift` (pure computation, identical on host and target).

> **Dialect fork.** This surface is recognised only by the standalone
> Compiler. Running one of these on a Family A REPL is a compile error
> ("undeclared name" for the file builtins / `random`, a parse error for
> `switch` and `for-in`-over-array) — that's intentional, not a bug. The REPLs
> sit at the 64K ceiling; the Compiler/Runner has the headroom.

## Running them

The boot disk is a Family B *compiler* disk and this data disk rides in
drive 2 (it must be **writable**, not write-protected — the Compiler writes
each `.swb` back onto it). In the launcher's file selector switch to the data
disk volume, enter `TESTS/` then `FBTESTS/`, highlight a test, and press **[X]**
to compile-and-run.

On the host, the build maps the same builtins onto stdio/POSIX. **`make
ondisk-host`** runs this whole tier (and `core/`) through the host build
automatically — each from a throwaway CWD, asserting `fail 0` — and is part of
`make ci`. To run one by hand (the tests write files to the **current
directory**, so use a scratch dir):

```
make host
mkdir -p /tmp/swiftii-fbtests && cd /tmp/swiftii-fbtests
for f in "$OLDPWD"/datadisk/tests/fbtests/*.swift; do
  "$OLDPWD"/build/host/swiftii_host "$f"
done
```

Expected last lines: `pass 17 fail 0` (tfileio), `pass 22 fail 0` (tfiledir),
`pass 12 fail 0` (tswitch), `pass 7 fail 0` (tforarr), `pass 3 fail 0`
(tscopefb), `pass 9 fail 0` (trandom), `pass 3 fail 0` (twait), `pass 18 fail
0` (tmath). The file tests
delete everything they create; the language tests touch no files. `twait` also
prints `wait 1 s ...` / `wait 2 s ...` / `wait 3 s ...` / `done` before its
summary — on a real (or emulated) target each line is followed by a pause of
its stated length, so the rising gaps are easy to see; on the host `wait()` is
a no-op, so they print instantly (same text either way).

The same surface is also covered automatically, off-target, by:
- the host C unit tests in `tests/unit/file_io_test.c` (`make test` / `make
  ci`), and
- the host integration fixture `tests/integration/800_fileio.swift` (`make
  integration` / `make ci`), which uses `/tmp` paths so it's hermetic.

These on-disk tests are the layer those two can't reach: real ProDOS MLI on a
real (or emulated) compiler-runner. **Verified on an emulated compiler-runner
2026-06-13** (both report `fail 0`); **`tmath` emulator-verified on II+ and //e
2026-06-17** (`pass 18 fail 0`).

## The files

These aim to be **exhaustive** for the Family-B-only surface — the happy path
*and* the edge/failure paths (the directories here are the authoritative test
suite; `progdisk/samples/` is just illustrative).

File / directory I/O (design doc 017):

- `tfileio.swift` — whole-file ops (17 checks): `writeFile` create + the
  **overwrite/truncate** semantic, `readFile` content length + the empty
  file (`some("")`, not `nil`) + the **missing file** (`nil`) case,
  `appendFile` growing a file *and* creating one when absent, `fileExists`,
  and `deleteFile` on a present then **missing** file.
- `tfiledir.swift` — directory ops (22 checks): `createDirectory` new vs
  **already-exists**, `listDirectory` on missing / empty / populated
  directories, `deleteDirectory` on a **non-empty** directory (ProDOS
  refuses → `false`) vs empty vs missing, and `renameFile` success vs a
  **missing source**.

Phase 16 big-language features (`WITH_BIGLANG` / `WITH_RANDOM`):

- `tswitch.swift` — `switch` on Int/Bool (12 checks): single + comma-grouped
  cases, `default`, implicit break (only the matched body runs), the
  no-match/no-default fall-through, and per-case local scoping.
- `tforarr.swift` — `for v in <array>` (7 checks): a plain walk, the empty
  array, a single element, iterating **after** `.append` (relocation), a
  `String` array, and nested for-in-over-array.
- `trandom.swift` — `random(in:)` (9 checks): range/coverage invariants for a
  closed `1...6` die, a half-open `0..<2` coin, a negative `-3..<0` range, and
  the degenerate `a...a`. The xorshift is fixed-seed, so the sequence is
  identical on host and target — the checks assert bounds + endpoint coverage,
  not a hardcoded sequence.

Timing:

- `twait.swift` — `wait(_ ms:)` delay (3 checks). A busy-wait has no output to
  assert, so the checks verify that execution **continues correctly** across
  `wait()` calls — a zero delay, a rising-gap pause loop, and accumulating
  state across delayed iterations — while the visible **1 / 2 / 3 second**
  pauses between the `wait N s ...` lines are the on-target proof it actually
  waited. `wait()` is a Family B *program* builtin (Compiler + Runner) — a
  delay belongs in compiled programs, not at an interactive prompt, so no REPL
  ships it. (Use a counted loop for rough pacing in a REPL.)

Math and strings:

- `tmath.swift` — `abs`/`sgn` (Int → Int) + `hasPrefix`/`hasSuffix` (String →
  Bool), 18 checks. `abs` over negative/positive/zero/expression; `sgn` over
  each sign + a symmetric-sum loop that cancels to 0; the string methods over
  happy paths plus the edges (empty needle always matches, a needle longer than
  the receiver never does, full-string match). Like `wait`/`tone` these are
  Family-B-only program builtins, so a Family A REPL rejects them.

### Also worth running here

The Runner executes the **extras** surface (`asc`/`chr`/`Int(_:)`, the array
methods) through a *different* code path than the Saturn/aux REPLs (normal
CODE vs an XLC overlay — a path that was a real shipped bug). The extras
tests in [`../xtests/`](../xtests) are source-compatible with the Runner, so
on a compiler-runner boot you can also open `TESTS/XTESTS/` and press `[X]` on
`XCONV` / `XARRAY` to verify that path with the tests you already have.
