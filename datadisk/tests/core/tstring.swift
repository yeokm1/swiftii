// tstring.swift - strings: +
// concatenation, \(...)
// interpolation, the String(Int)
// conversion, .count / .isEmpty,
// escape sequences, and the notable
// edge case that == on strings is
// REFERENCE identity (two equal
// literals are NOT interned, so "hi"
// == "hi" is false - compare by
// .count or by interpolating into
// the printed line instead). Runs on
// ANY SwiftII REPL. Prints "pass N
// fail 0".

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

// Concatenation: count is the sum of
// the parts.
let w = "apple" + "soft"
chk(w.count, 9)
chk(("" + "x").count, 1)

// .count is byte length; .isEmpty
// tests for "".
chk("hello".count, 5)
chkb("".isEmpty, true)
chkb("x".isEmpty, false)

// Escapes are single bytes: \n \t \\
// \" each count as one.
chk("\n".count, 1)
chk("\t".count, 1)
chk("\\".count, 1)
chk("\"".count, 1)
chk("a\tb".count, 3)

// String(Int) and interpolation
// length must agree, including the
// sign.
chk(String(0).count, 1)
chk(String(42).count, 2)
// '-' plus a digit
chk(String(-7).count, 2)
chk("n=\(123)".count, 5) // "n=123"

// EDGE CASE: == on strings is
// reference identity, NOT value
// equality. Even two identical
// literals are distinct heap strings
// here.
let a = "same"
let b = "same"
// surprising but documented
chkb(a == b, false)
// a reference equals itself
chkb(a == a, true)

print("pass \(npass) fail \(nfail)")
