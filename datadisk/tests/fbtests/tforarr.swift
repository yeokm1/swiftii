// tforarr.swift - for-in over an
// array (Phase 16 stretch).
// FAMILY B ONLY: compiler disk,
// FBTESTS/, press [X]. The REPLs
// reject `for v in <array>` (compile
// error) - the deliberate dialect
// fork. "fail 0" on the last line =
// pass. Covers a plain walk, the
// empty array, a single element,
// iterating after .append
// (relocation), a String array, and
// nested for-in-over-array.

var npass = 0
var nfail = 0
func chk(_ g: Int, _ w: Int) {
  if g == w {
    npass = npass + 1
  } else {
    nfail = nfail + 1
    print("FAIL got \(g) want \(w)")
  }
}

// Plain walk: sum the elements.
func sumArr() -> Int {
  let xs = [10, 20, 30, 40]
  var s = 0
  for v in xs { s = s + v }
  return s
}
chk(sumArr(), 100)

// Empty array: the body never runs.
func emptyArr() -> Int {
  let xs: [Int] = []
  var n = 0
  for v in xs { n = n + 1 }
  return n
}
chk(emptyArr(), 0)

// Single element.
func oneArr() -> Int {
  let xs = [42]
  var s = 0
  for v in xs { s = s + v }
  return s
}
chk(oneArr(), 42)

// Iterate AFTER .append (the array
// may have relocated): [1,2,3,4] ->
// 10.
func appendArr() -> Int {
  var xs = [1, 2, 3]
  xs.append(4)
  var s = 0
  for v in xs { s = s + v }
  return s
}
chk(appendArr(), 10)

// Element count via the walk matches
// the known length.
func countArr() -> Int {
  let xs = [5, 5, 5, 5, 5]
  var n = 0
  for v in xs { n = n + 1 }
  return n
}
chk(countArr(), 5)

// String array: total characters
// across all elements.
func strArr() -> Int {
  let names = ["ab", "cde", "f"]
  var total = 0
  for s in names {
    total = total + s.count
  }
  return total
}
chk(strArr(), 6)

// Nested for-in over two arrays:
// (1+2+3) * (10+20) = 180.
func nested() -> Int {
  let rows = [1, 2, 3]
  let cols = [10, 20]
  var s = 0
  for r in rows {
    for c in cols { s = s + r * c }
  }
  return s
}
chk(nested(), 180)

print("pass \(npass) fail \(nfail)")
