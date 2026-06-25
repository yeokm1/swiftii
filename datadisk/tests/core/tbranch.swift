// tbranch.swift - if/else if/else
// inside a TOP-LEVEL while loop. No
// modulo, no strings. The middle
// branches (i==2, i==3) each take
// the "jump over the rest to the
// end" path; i==1,4,5 fall to else.
// Runs on ANY SwiftII REPL.
// Self-checking: one FAIL line per
// wrong result and a final
// "pass N fail 0".
//
// The loop is top-level on purpose -
// that is the codegen path this
// guards. Wanted values come from an
// independent table.

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

// per i in 1...5 -> 1 20 30 4 5
var want = [1, 20, 30, 4, 5]
var i = 1
while i <= 5 {
  var v = 0
  if i == 2 {
    v = 20
  } else if i == 3 {
    v = 30
  } else {
    v = i
  }
  chk(v, want[i - 1])
  i = i + 1
}

print("pass \(npass) fail \(nfail)")
