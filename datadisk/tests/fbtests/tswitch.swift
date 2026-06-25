// tswitch.swift - switch on
// Int/Bool (Phase 16). FAMILY B
// ONLY (see README); the REPLs
// reject switch. "fail 0" = pass.
// Covers single/comma cases,
// default, break, no-match, Bool,
// per-case scoping.

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

// Int: single value, comma-grouped
// values, and default.
func classify(_ n: Int) -> Int {
  switch n {
  case 0: return 100
  case 1, 2, 3: return 200
  case 10: return 300
  default: return 999
  }
}
chk(classify(0), 100)
chk(classify(1), 200) // comma group
chk(classify(3), 200) // comma group
chk(classify(10), 300)
chk(classify(99), 999)  // default

// Implicit break: only the matched
// case body runs, no fall-through.
func once(_ n: Int) -> Int {
  var h = 0
  switch n {
  case 1: h = h + 1
  case 2: h = h + 1
  case 3: h = h + 1
  default: h = h + 1
  }
  return h
}
chk(once(2), 1)

// No default and no match: nothing
// runs, control continues past it.
func nomatch(_ n: Int) -> Int {
  var m = 0
  switch n {
  case 1: m = 1
  case 2: m = 2
  }
  // proves we reached here despite
  // no match
  return m + 5
}
chk(nomatch(7), 5)

// switch as a statement, nested in
// a loop, writing an accumulator.
func runloop() -> Int {
  var acc = 0
  var i = 0
  while i < 5 {
    switch i {
    case 0: acc = acc + 1
    case 2, 4: acc = acc + 10
    default: acc = acc + 100
    }
    i = i + 1
  }
  // 1+100+10+100+10 = 221
  return acc
}
chk(runloop(), 221)

// Bool cases.
func boolcode(_ b: Bool) -> Int {
  var r = 0
  switch b {
  case true: r = 1
  case false: r = 0
  }
  return r
}
chk(boolcode(true), 1)
chk(boolcode(false), 0)

// Per-case local scoping: each body
// owns its own `v`.
func pick(_ n: Int) -> Int {
  switch n {
  case 1:
    let v = 100
    return v
  default:
    let v = 200
    return v
  }
}
chk(pick(1), 100)
chk(pick(5), 200)

print("pass \(npass) fail \(nfail)")
