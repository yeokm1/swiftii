// tscope.swift - a `let`/`var` declared
// in a CONDITIONALLY-taken branch (if /
// else) within a loop, inside a FUNCTION,
// must be popped on that branch's own
// path. Deferring to the loop's cleanup
// drifts the value stack on the skipped
// iterations - was "VM halted with error"
// (SE_TYPE_MISMATCH), fixed by parse_if ->
// parse_block_scoped (design 020). Runs on
// any REPL + every Family B tier. Prints
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

// bubble sort in a function: the if-body
// `let tmp` is taken on early passes and
// SKIPPED once the row is sorted.
func sortends() -> Int {
  var a = [31, 4, 15, 92, 65, 35, 8, 97, 9, 27]
  var swapped = true
  while swapped {
    swapped = false
    for i in 0..<a.count - 1 {
      if a[i] > a[i + 1] {
        let tmp = a[i]
        a[i] = a[i + 1]
        a[i + 1] = tmp
        swapped = true
      }
    }
  }
  return a[0] + a[9]
}
chk(sortends(), 101)

// conditional `let`, non-array condition,
// if taken only on i == 0.
func condlet() -> Int {
  var sum = 0
  for i in 0..<6 {
    if i == 0 {
      let bonus = 100
      sum = sum + bonus
    }
    sum = sum + i
  }
  return sum
}
chk(condlet(), 115)

// conditional `let` in the ELSE branch
// (the else arm is scoped too).
func condelse() -> Int {
  var sum = 0
  for i in 0..<5 {
    if i == 2 {
      sum = sum + 1000
    } else {
      let step = i + 1
      sum = sum + step
    }
  }
  return sum
}
chk(condelse(), 1012)

// while around for, conditional `var` in
// the if, all inside a function.
func nested() -> Int {
  var total = 0
  var rounds = 0
  while rounds < 3 {
    for i in 0..<4 {
      if i > rounds {
        var v = i + i
        total = total + v
      }
    }
    rounds = rounds + 1
  }
  return total
}
chk(nested(), 28)

print("pass \(npass) fail \(nfail)")
