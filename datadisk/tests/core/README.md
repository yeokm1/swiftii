# datadisk/tests/core/

The **general** tier: self-checking tests that run on **any** SwiftII REPL
(lite or extras) and on the Family B runner. See [`../README.md`](../README.md)
for the `chk` harness, how to run a tier, and the per-test constraints; the
demos in [`progdisk/samples/`](../../../progdisk/samples) ship under
`SAMPLES/` on the same disk.

## The files

- `tarith.swift` — i16 overflow wraparound, truncate-toward-zero division,
  C-style modulo sign, precedence, the u16-literal storage rule
  (`49200` → `-16336`).
- `tcontrol.swift` — `if`/`else if`/`else`, `while`, `for-in` over `..<`
  and `...`, `break`, and the nested-loop / nested-`if` cases from the
  2026-06-03 cc65 fix.
- `tassign.swift` — compound assignment (`+=`/`-=`/`*=`/`/=`) on globals and
  locals, in a loop body, and the `String +=` concat path.
- `tfunc.swift` — positional calls, multi-param functions, recursion within
  the 4-frame cap, `min`/`max`, a helper that writes a global, and the
  in-function `for-in`+`while`+local-`var` regression (2026-06-06 fix).
- `tscope.swift` — a `let`/`var` declared in a **conditionally-taken** `if`/
  `else` branch inside a loop, in a function, popped on its own branch path
  (bubble sort, conditional `let`, `else`-branch `let`, nested `while`/`for`).
  The branch-scoping fix; before it these drifted the value stack on the
  skipped iterations and aborted with `SE_TYPE_MISMATCH`.
- `tstring.swift` — `+`, `\(…)` interpolation, `String(Int)`, `.count`,
  `.isEmpty`, escape-byte counts, and the edge case that `==` on strings is
  **reference identity** (`"hi" == "hi"` is `false`).
- `tarray.swift` — literals, `.count`/`.isEmpty`, subscript read/write, and
  the `append` write-back rule (statement vs expression form).
- `toptional.swift` — `if let`, `if let … else`, `??`, force-unwrap `!`,
  and an `Int?`-returning function on both the some and nil paths.
- `tlogic.swift` — short-circuit `&&` / `||`: the truth tables, precedence
  (`&&` binds tighter than `||`, comparison tighter than both, prefix `!`
  tightest), and — via a call-counter probe — that the rhs is evaluated
  only when the lhs doesn't already decide the result.
- `tbranch.swift` — `if`/`else if`/`else` in a **top-level** `while`, the
  "jump over the rest to the end" codegen path.
- `tmod.swift` — `%` in a top-level `while` body (checked against an
  independent table, not `%` again).
- `tstrint.swift` — a `String`-operand branch with a jump-over then an `Int`
  else, in a top-level `while`; counts which branch ran.

The last three are codegen regression diagnostics (moved here from
`../samples/` 2026-06-11 to free program-disk space). They were print-only
known-good sequences until 2026-06-18, when they were converted to the
self-checking harness and renamed with the `t` prefix so the **Run tests** sweep gets
a real verdict from them — a print-only file always reads as a false PASS on
the Family B `TESTLOG` watcher. (A fourth, `nested.swift`, was dropped the
same day: `tcontrol.swift` already covers its 2026-06-03 case.)

The **extras** tests live in [`../xtests/`](../xtests); the **Family B**
file/directory I/O tests live in [`../fbtests/`](../fbtests).
