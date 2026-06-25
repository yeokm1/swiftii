// tmath.swift - abs / sgn (Int->Int)
// and hasPrefix / hasSuffix (String
// -> Bool). All FAMILY B
// (compiler-runner) ONLY: program
// builtins, so the Family A REPLs
// reject them - boot a compiler disk
// and press [X]. Pure computation,
// no hardware, identical on host and
// target. Last line "fail 0" = pass.

var npass = 0
var nfail = 0
func chk(_ g: Int, _ w: Int) {
  if g == w { npass = npass + 1 }
  else {
    nfail = nfail + 1
    print("FAIL got \(g) want \(w)")
  }
}
func chkb(_ g: Bool, _ w: Bool) {
  if g == w { npass = npass + 1 }
  else {
    nfail = nfail + 1
    print("FAILb")
  }
}

// 1) abs: negative, positive, zero.
chk(abs(0 - 7), 7)
chk(abs(9), 9)
chk(abs(0), 0)

// 2) abs of an expression + a var.
var v = 3 - 50
chk(abs(v), 47)

// 3) sgn: negative, zero, positive.
chk(sgn(0 - 4), 0 - 1)
chk(sgn(0), 0)
chk(sgn(12), 1)

// 4) state survives across calls.
//    sgn over -3..3 is symmetric, so
//    the signs cancel to 0.
var sum = 0
var i = 0 - 3
while i < 4 {
  sum = sum + sgn(i)
  i = i + 1
}
chk(sum, 0)

// 5) hasPrefix / hasSuffix happy
//    paths.
chkb("hello.swift".hasSuffix(".swift"), true)
chkb("hello".hasPrefix("he"), true)
chkb("hello".hasPrefix("xy"), false)
chkb("hello".hasSuffix("lo"), true)
chkb("hello".hasSuffix("hi"), false)

// 6) edges: empty needle matches; a
//    needle longer than the receiver
//    never does; full-string match.
chkb("ab".hasPrefix(""), true)
chkb("ab".hasSuffix(""), true)
chkb("ab".hasPrefix("abc"), false)
chkb("ab".hasSuffix("zab"), false)
chkb("ab".hasPrefix("ab"), true)

print("pass \(npass) fail \(nfail)")
