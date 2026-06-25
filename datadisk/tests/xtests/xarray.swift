// xarray.swift - EXTRAS ONLY (x
// prefix). The array methods
// removeLast, removeAll, and
// contains are XLC builtins on
// SWIFTSAT ([S]) / SWIFTAUX ([A])
// only; lite rejects them as
// "unknown member" at compile time.
// On the host they all work. Prints
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

// removeLast returns the popped
// element and shrinks the array in
// place.
var xs = [1, 2, 3]
let last = xs.removeLast()
chk(last, 3)
chk(xs.count, 2)
chk(xs[1], 2)

// contains uses == value equality
// for Int (reference identity for
// strings - see tstring.swift - so
// this is the Int case).
chkb(xs.contains(1), true)
chkb(xs.contains(2), true)
// already removed
chkb(xs.contains(3), false)

// removeLast down to one, then empty
// via removeAll.
let one = xs.removeLast()
chk(one, 2)
chk(xs.count, 1)
xs.removeAll()
chkb(xs.isEmpty, true)
chk(xs.count, 0)
// nothing left to find
chkb(xs.contains(1), false)

print("pass \(npass) fail \(nfail)")
