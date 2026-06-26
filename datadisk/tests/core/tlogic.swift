// tlogic.swift - short-circuit && and
// || : truth tables, precedence (&&
// binds tighter than ||, comparison
// tighter than both), and that the
// rhs is evaluated ONLY when the lhs
// does not already decide the result.
// Runs on ANY SwiftII REPL. Prints
// "pass N fail 0".

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

// Side-effect probes: each bumps the
// call counter, so a skipped rhs is
// observable as an unchanged count.
var calls = 0
func tru() -> Bool {
  calls = calls + 1
  return true
}
func fls() -> Bool {
  calls = calls + 1
  return false
}

// Truth tables.
chkb(true && true, true)
chkb(true && false, false)
chkb(false && true, false)
chkb(false && false, false)
chkb(true || true, true)
chkb(true || false, true)
chkb(false || true, true)
chkb(false || false, false)

// Precedence: && binds tighter than
// ||, so false || true && false is
// false || (true && false) == false.
chkb(false || true && false, false)
// Parens override: (false || true)
// && false == false.
chkb((false || true) && false, false)
// Comparison binds tighter than both.
chkb(1 < 2 && 2 < 3, true)
chkb(1 > 2 || 3 < 4, true)
// Prefix ! binds tighter still.
chkb(!false && true, true)

// Short-circuit: rhs is skipped when
// the lhs alone decides the result.
calls = 0
chkb(false && tru(), false)
chk(calls, 0)
chkb(true || tru(), true)
chk(calls, 0)

// ...and the rhs runs when the lhs
// leaves the result open.
calls = 0
chkb(true && tru(), true)
chk(calls, 1)
chkb(false || fls(), false)
chk(calls, 2)

print("pass \(npass) fail \(nfail)")
