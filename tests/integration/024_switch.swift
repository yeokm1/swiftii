// switch (Phase 16 stretch — Family B / host only).
// Int + Bool, single and comma-grouped cases, default, fall-through,
// per-case return, and nesting inside a loop.

func classify(_ n: Int) -> String {
  switch n {
  case 0:
    return "zero"
  case 1, 2, 3:
    return "small"
  case 10:
    return "ten"
  default:
    return "other"
  }
}
print(classify(0))     // zero
print(classify(2))     // small
print(classify(3))     // small
print(classify(10))    // ten
print(classify(99))    // other

// switch as a statement, nested in a loop
var i = 0
while i < 5 {
  switch i {
  case 0: print("a")
  case 2, 4: print("c")
  default: print("-")
  }
  i = i + 1
}

// no default, no match: falls through cleanly (nothing printed)
let g = 7
switch g {
case 1: print("no")
case 7: print("yes")
}
switch g {
case 100: print("unreached")
}
print("after")

// Bool
let flag = false
switch flag {
case true: print("T")
case false: print("F")
}

// case-body locals are scoped per case (compile-time slot reuse)
func pick(_ n: Int) -> Int {
  switch n {
  case 1:
    let v = 100
    return v
  default:
    let v = 200
    return v
  }
}
print(pick(1))    // 100
print(pick(5))    // 200
