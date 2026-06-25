// xconv.swift - EXTRAS ONLY (the x
// prefix marks it). asc / chr /
// Int(s) are XLC builtins present
// only on SWIFTSAT (II+ disk, [S])
// and SWIFTAUX (//e disk, [A]); the
// lite binaries reject them at
// COMPILE time ("undeclared name"),
// so run this on an extras REPL. On
// the host all three work. Prints
// "pass N fail 0".
//
// Focus: the Int(s) parser's edge
// cases - the nil-returning ones are
// where a parser tends to go wrong.

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

// asc: ASCII value of the first byte
// (ignores the rest).
chk(asc("A"), 65)
chk(asc("a"), 97)
chk(asc("AB"), 65)
chk(asc("0"), 48)

// chr: single-character string for a
// byte; chr(asc(s)) round-trips.
chk(chr(66).count, 1)
chk(asc(chr(66)), 66)
chk(asc(chr(asc("Z"))), 90)

// Int(s) success cases, including
// the optional leading sign.
chk(Int("0") ?? -999, 0)
chk(Int("42") ?? -999, 42)
chk(Int("+5") ?? -999, 5)
chk(Int("-5") ?? -999, -5)
chk(Int("32767") ?? -999, 32767)
chk(Int("-32768") ?? -999, -32768)

// Int(s) nil cases - these are the
// edge cases that trip a parser.
chkb(Int("") == nil, true) // empty
// trailing junk
chkb(Int("3x") == nil, true)
// non-numeric
chkb(Int("x") == nil, true)
// a LEADING SPACE is rejected
chkb(Int(" 42") == nil, true)
// out of i16 range
chkb(Int("99999") == nil, true)
// just under the min
chkb(Int("-32769") == nil, true)

print("pass \(npass) fail \(nfail)")
