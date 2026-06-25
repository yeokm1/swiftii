// tscopefb.swift - the value-stack scoping
// invariant for the Family-B-only body
// types (switch case, for-in-array),
// companion to core/tscope.swift (if /
// else / while / for-range). A `let`/`var`
// declared in a conditionally-taken body
// must be popped on that body's own path,
// or it drifts the stack on the skipped
// iterations - the shared
// parse_block_scoped_auto helper (design
// 020). Prints "pass N fail 0".

var npass = 0
var nfail = 0
func chk(_ g: Int, _ w: Int) {
  if g == w {
    npass = npass + 1
  } else {
    nfail = nfail + 1
    print("FAIL got \(g) want \(w)")
  }
}

// switch inside a loop: the case-body `let`
// is taken on i == 2, 4 and skipped on the
// rest.
func loopcase() -> Int {
  var total = 0
  var i = 0
  while i < 6 {
    switch i {
    case 2, 4:
      let bump = i + 100
      total = total + bump
    default:
      total = total + 1
    }
    i = i + 1
  }
  return total
}
chk(loopcase(), 210)

// for-in-array with a conditional `let` in
// the body, taken only for v > 4.
func condbody() -> Int {
  let xs = [3, 8, 1, 9, 2]
  var total = 0
  for v in xs {
    if v > 4 {
      let big = v + 100
      total = total + big
    }
    total = total + 1
  }
  return total
}
chk(condbody(), 222)

// for-in-array whose body is a switch with
// a per-case local: two scoped body types
// nested in a function loop.
func arrswitch() -> Int {
  let xs = [1, 2, 3, 2, 1]
  var total = 0
  for v in xs {
    switch v {
    case 2:
      let two = v * 10
      total = total + two
    default:
      let other = v
      total = total + other
    }
  }
  return total
}
chk(arrswitch(), 45)

print("pass \(npass) fail \(nfail)")
