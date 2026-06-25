// tarray.swift - arrays: literals,
// .count / .isEmpty, subscript read
// and write, and append. NOTE the
// append write-back rule: a
// statement-level xs.append(v)
// updates xs in place, but in
// expression position you must
// capture the (possibly relocated)
// result - `xs = xs.append(v)`. Runs
// on ANY SwiftII REPL. Prints "pass
// N fail 0".

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
func chkb(_ got: Bool, _ want: Bool) {
  if got == want {
    npass = npass + 1
  } else {
    nfail = nfail + 1
    print("FAIL bool want \(want)")
  }
}

// Literal, count, subscript read.
let xs = [10, 20, 30]
chk(xs.count, 3)
chk(xs[0], 10)
chk(xs[2], 30)
chkb(xs.isEmpty, false)

// Empty array.
let none: [Int] = []
chkb(none.isEmpty, true)
chk(none.count, 0)

// Subscript write updates one slot
// in place.
var ys = [1, 2, 3]
ys[1] = 99
chk(ys[1], 99)
chk(ys[0], 1) // neighbours untouched
chk(ys.count, 3)

// Append: statement form mutates in
// place...
var zs = [1]
zs.append(2)
zs.append(3)
chk(zs.count, 3)
chk(zs[2], 3)

// ...expression form returns the
// array reference (capture to
// update).
var ws = [5]
ws = ws.append(6)
chk(ws[1], 6)
chk(ws.count, 2)

// Walk by the index range to sum and
// find the largest.
var sum = 0
var big = xs[0]
for i in 0..<xs.count {
  sum = sum + xs[i]
  big = max(big, xs[i])
}
chk(sum, 60)
chk(big, 30)

print("pass \(npass) fail \(nfail)")
