// tstrint.swift - a STRING branch
// (with jump-over) then an int else,
// in a TOP-LEVEL while loop: codegen
// of a branch whose body has a
// string operand, and the jump over
// the else. Runs on ANY SwiftII
// REPL. Self-checking: one FAIL line
// per wrong result and a final
// "pass N fail 0".
//
// It still prints "two" so the
// visual sequence (1 two 3 4 5) is
// intact, but it also counts which
// branch ran: the string branch must
// fire exactly once (i==2) and the
// else for the other four iterations
// - that is what catches a broken
// jump-over.

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

// times the string branch ran
var twoCount = 0
// sum of i over the else branch
var elseSum = 0
var i = 1
while i <= 5 {
  if i == 2 {
    print("two")
    twoCount = twoCount + 1
  } else {
    elseSum = elseSum + i
  }
  i = i + 1
}
// string branch ran only at i==2
chk(twoCount, 1)
// else ran for 1+3+4+5 (NOT +2)
chk(elseSum, 13)

print("pass \(npass) fail \(nfail)")
