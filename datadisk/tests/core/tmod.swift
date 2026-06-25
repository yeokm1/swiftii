// tmod.swift - modulo (%) inside a
// top-level while loop: codegen of
// the remainder op in a loop body.
// Runs on ANY SwiftII REPL.
// Self-checking: one FAIL line per
// wrong result and a final
// "pass N fail 0".
//
// The loop runs at TOP LEVEL on
// purpose (each top-level loop var
// takes a global slot; the cap is
// 32). The wanted values come from
// an independent table, not from %
// again, so the check is not a
// tautology.

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

// i % 3 for i in 1...5 -> 1 2 0 1 2
var want = [1, 2, 0, 1, 2]
var i = 1
while i <= 5 {
  chk(i % 3, want[i - 1])
  i = i + 1
}

print("pass \(npass) fail \(nfail)")
