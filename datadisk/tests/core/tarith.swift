// tarith.swift - integer arithmetic
// edge cases. Runs on ANY SwiftII
// REPL (lite or extras).
// Self-checking: it prints one FAIL
// line per wrong result and a final
// "pass N fail 0" tally. On target
// read the last line.
//
// Covers the 16-bit signed (i16)
// wraparound, truncate-toward-zero
// division, C-style modulo sign,
// unary minus, precedence, and the
// u16 literal storage rule (49200 is
// kept as its two's-complement i16,
// so it prints as -16336 - see
// docs/using/LANGUAGE.md "Literals").

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

// Basic operators.
// precedence: * before +
chk(2 + 3 * 4, 14)
// parens override
chk((2 + 3) * 4, 20)
// left-associative
chk(20 - 4 - 3, 13)
chk(-5 + 3, -2) // unary minus
chk(- -7, 7) // double negate

// Integer division truncates toward
// zero (not floor).
chk(7 / 2, 3)
chk(-7 / 2, -3) // -3, NOT -4
chk(7 / -2, -3)

// Modulo: the result takes the sign
// of the dividend (C semantics).
chk(7 % 3, 1)
chk(-7 % 3, -1)
chk(7 % -3, 1)

// i16 wraparound at the signed
// boundaries.
chk(32767 + 1, -32768)
chk(-32768 - 1, 32767)

// A literal in 32768..65535 is
// stored as its two's-complement
// i16, so 49200 ($C030, the speaker)
// prints as -16336.
chk(49200, -16336)

print("pass \(npass) fail \(nfail)")
