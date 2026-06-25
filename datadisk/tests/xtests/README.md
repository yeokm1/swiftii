# datadisk/tests/xtests/

The **extras** tier: tests for builtins that exist only on a SWIFTSAT (II+
Saturn) or SWIFTAUX (//e aux) **extras REPL**. The lite REPLs reject these at
compile time; the host build has them. Needs a Saturn / aux boot (or a
compiler-runner — see [`../fbtests/`](../fbtests)). See
[`../README.md`](../README.md) for the `chk` harness, run targets, and
constraints.

## The files

- `xconv.swift` — `asc`, `chr`, `chr(asc(s))` round-trip, and the `Int(s)`
  parser's nil edge cases (empty, trailing junk, non-numeric, a leading
  space, out of i16 range).
- `xarray.swift` — `removeLast`, `removeAll`, `contains`.
