// toptional.swift - optionals:
// if-let, if-let-else, the ??
// default, and force-unwrap (!) on a
// known-some value. A function
// returning Int? drives the nil and
// some paths. Runs on ANY SwiftII
// REPL. Prints "pass N fail 0".

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

// 1-based index of target in xs, or
// nil if absent.
func indexOf(_ target: Int, _ xs: [Int]) -> Int? {
  var i = 0
  while i < xs.count {
    if xs[i] == target {
      return i + 1
    }
    i = i + 1
  }
  return nil
}

let xs = [4, 8, 15, 16, 23]

// if-let takes the some path.
var found = 0
if let pos = indexOf(15, xs) {
  found = pos
}
chk(found, 3)

// if-let-else takes the else arm on
// nil.
var got = -1
if let pos = indexOf(99, xs) {
  got = pos
} else {
  got = 0
}
chk(got, 0)

// ?? supplies a default only when
// the optional is nil.
// some -> the value
chk(indexOf(16, xs) ?? -1, 4)
// nil -> the default
chk(indexOf(99, xs) ?? -1, -1)

// Force-unwrap a known-some
// optional.
let m: Int? = 42
chk(m!, 42)

// A function whose result is
// unwrapped inside if-let in the
// caller.
func half(_ n: Int) -> Int? {
  if n % 2 == 0 { return n / 2 }
  return nil
}
var h = -1
if let v = half(10) {
  h = v
} else {
  h = 0
}
chk(h, 5)
var h2 = -1
if let v = half(7) {
  h2 = v
} else {
  h2 = 0
}
chk(h2, 0)

print("pass \(npass) fail \(nfail)")
