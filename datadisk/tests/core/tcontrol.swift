// tcontrol.swift - control flow: if
// / else if / else, while, for-in
// over both range forms, break, and
// the nested-loop / nested-if cases
// that a 2026-06-03 cc65 miscompile
// got wrong. Runs on ANY SwiftII
// REPL. Prints "pass N fail 0".
//
// The nested LOOPS run at top level
// on purpose - that is the path the
// 2026-06-03 fix verified. The
// single-loop helpers are wrapped
// in functions so their loop
// variable does not consume a
// global slot (the cap is 32).

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

// if / else if / else, all three
// arms.
func classify(_ n: Int) -> Int {
  if n > 0 { return 1 }
  else if n < 0 { return -1 }
  else { return 0 }
}
chk(classify(5), 1)
chk(classify(-5), -1)
chk(classify(0), 0)

// for-in half-open then closed
// (single loop - safe inside a
// function).
func sumHalfOpen(_ n: Int) -> Int {
  var a = 0
  for i in 0..<n { a = a + i }
  return a
}
func sumClosed(_ n: Int) -> Int {
  var a = 0
  for i in 1...n { a = a + i }
  return a
}
chk(sumHalfOpen(5), 10) // 0+1+2+3+4
chk(sumClosed(5), 15) // 1+2+3+4+5

// while + break: stop early.
func countTo(_ limit: Int) -> Int {
  var i = 0
  var seen = 0
  while true {
    i = i + 1
    if i > limit { break }
    seen = seen + 1
  }
  return seen
}
chk(countTo(3), 3)

// if-no-else with a nested if,
// every path.
func cl(_ x: Int) -> Int {
  if x > 0 {
    if x > 10 { return 2 }
    return 1
  }
  return 0
}
chk(cl(5), 1)
chk(cl(50), 2)
chk(cl(-1), 0)

// Nested loops at TOP LEVEL (the
// verified path). Outer loop must
// run fully.
var g = 0
for x in 1...3 {
  for y in 1...3 { g = g + 1 }
}
chk(g, 9)

// for-in containing a while, with
// a loop-body-local var:
// 1+2+3+4 = 10.
var m = 0
for k in 1...4 {
  var c = 0
  while c < k {
    m = m + 1
    c = c + 1
  }
}
chk(m, 10)

print("pass \(npass) fail \(nfail)")
