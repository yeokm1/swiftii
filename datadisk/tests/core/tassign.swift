// tassign.swift - compound
// assignment += -= *= /= on globals
// and locals, plus the String +=
// concat path. Runs on ANY SwiftII
// REPL (the fix ships on every
// binary as of 2026-06-06). Prints
// "pass N fail 0".
//
// Note: the harness counters use
// plain `n = n + 1` (not +=) on
// purpose, so a build where compound
// assignment is broken still tallies
// correctly and the failures show up
// as content, not a miscount.

var npass = 0
var nfail = 0
func chk(_ got: Int, _ want: Int) {
  if got == want {
    npass = npass + 1
  } else {
    nfail = nfail + 1
    print("FAIL got \(got) want \(want)")
  }
}

// Globals, each operator once, left
// to right.
var g = 0
g += 5
chk(g, 5)
g -= 2
chk(g, 3)
g *= 4
chk(g, 12)
g /= 3
chk(g, 4)

// Locals inside a function.
func compute() -> Int {
  var n = 10
  n += 1          // 11
  n *= 2          // 22
  n -= 3          // 19
  n /= 2 // 9 (integer division)
  return n
}
chk(compute(), 9)

// Compound on a loop accumulator
// (local), exercising it in a loop
// body.
func sumTo(_ k: Int) -> Int {
  var t = 0
  for i in 1...k { t += i }
  return t
}
chk(sumTo(5), 15)

// String += concatenates (compiles
// to the same path as +).
var s = "ab"
s += "cde"
chk(s.count, 5)
s += ""
chk(s.count, 5)

print("pass \(npass) fail \(nfail)")
