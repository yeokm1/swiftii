// tfunc.swift - functions:
// positional args, multiple params,
// bounded recursion (the VM caps the
// call stack at 4 frames, so
// factorial(4) is the deepest safe
// call), and the min / max builtins.
// A helper that mutates the global
// tally also exercises a function
// writing a global. Runs on ANY
// SwiftII REPL. Prints "pass N fail
// 0".

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

func square(_ x: Int) -> Int {
  return x * x
}
func power(_ base: Int, _ exp: Int) -> Int {
  var r = 1
  var i = 0
  while i < exp {
    r = r * base
    i = i + 1
  }
  return r
}
// Recursion kept within the 4-frame
// cap: factorial(4) is the deepest.
func fact(_ n: Int) -> Int {
  if n <= 1 { return 1 }
  return n * fact(n - 1)
}

chk(square(9), 81)
chk(power(2, 10), 1024)
// empty loop -> identity
chk(power(5, 0), 1)
chk(fact(1), 1)
chk(fact(4), 24)

// Calls nest as arguments; min / max
// are builtins.
// min(9, 16)
chk(min(square(3), power(2, 4)), 9)
chk(max(-5, 5), 5)
chk(min(7, 7), 7) // equal args
// square(8)
chk(square(power(2, 3)), 64)

// Regression (2026-06-06): a for-in
// containing a while whose body
// declares a local var must work
// INSIDE a function - the for-in
// parks range_end on the value
// stack, which used to shift the
// body local's slot and make this
// return 0. Now correct on every
// binary.
func triangle(_ n: Int) -> Int {
  var m = 0
  for k in 1...n {
    var c = 0
    while c < k {
      m = m + 1
      c = c + 1
    }
  }
  return m
}
chk(triangle(4), 10) // 1+2+3+4
chk(triangle(1), 1)

print("pass \(npass) fail \(nfail)")
