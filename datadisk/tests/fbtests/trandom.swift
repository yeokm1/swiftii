// trandom.swift - random(in:)
// invariants (Phase 16, item 23).
// FAMILY B ONLY (see README); the
// REPLs reject random(in:). The
// xorshift is fixed-seed, so these
// are range/coverage invariants,
// not a hardcoded sequence.

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
func chkb(_ g: Bool, _ w: Bool) {
  if g == w {
    npass = npass + 1
  } else {
    nfail = nfail + 1
    print("FAIL bool")
  }
}

// 1...6 over 120 rolls: every roll
// in [1,6], both endpoints reached.
func dice() {
  var lo = 99
  var hi = 0 - 99
  var bad = 0
  var i = 0
  while i < 120 {
    let d = random(in: 1...6)
    if d < lo { lo = d }
    if d > hi { hi = d }
    if d < 1 { bad = bad + 1 }
    if d > 6 { bad = bad + 1 }
    i = i + 1
  }
  // never out of range
  chk(bad, 0)
  // low endpoint reached
  chk(lo, 1)
  // high endpoint reached
  chk(hi, 6)
}
dice()

// 0..<2 over 200 flips: only 0/1,
// both seen, all counted.
func coin() {
  var c0 = 0
  var c1 = 0
  var other = 0
  var i = 0
  while i < 200 {
    let b = random(in: 0..<2)
    if b == 0 { c0 = c0 + 1 }
    else if b == 1 { c1 = c1 + 1 }
    else { other = other + 1 }
    i = i + 1
  }
  // nothing outside {0,1}
  chk(other, 0)
  // every flip counted
  chk(c0 + c1, 200)
  chkb(c0 > 0, true)   // 0 seen
  chkb(c1 > 0, true)   // 1 seen
}
coin()

// -3..<0 over 60 draws: each value
// in {-3,-2,-1}.
func neg() {
  var bad = 0
  var i = 0
  while i < 60 {
    let v = random(in: -3..<0)
    if v < (0 - 3) { bad = bad + 1 }
    if v > (0 - 1) { bad = bad + 1 }
    i = i + 1
  }
  chk(bad, 0)
}
neg()

// degenerate a...a always returns a
// (span 1).
func single() {
  var bad = 0
  var i = 0
  while i < 10 {
    if random(in: 7...7) == 7 {
      bad = bad + 0
    }
    else { bad = bad + 1 }
    i = i + 1
  }
  chk(bad, 0)
}
single()

print("pass \(npass) fail \(nfail)")
