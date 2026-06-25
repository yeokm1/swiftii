// Phase 9 — if-let `else` branch + if-let inside function bodies.

// 1. Top-level if-let with else, some path.
let a: Int? = 5
if let v = a {
  print("a-some \(v)")
} else {
  print("a-none")
}

// 2. Top-level if-let with else, nil path.
let b: Int? = nil
if let v = b {
  print("b-some \(v)")
} else {
  print("b-none")
}

// 3. if-let inside a function body, both arms, with a nested local in
//    the bound branch (scope-pop must not corrupt the frame).
func describe(_ x: Int?) -> String {
  if let n = x {
    let doubled = n * 2
    return "got \(n) x2=\(doubled)"
  } else {
    return "nothing"
  }
}
print(describe(7))
print(describe(nil))

// 4. if-let inside a function, no else, body declares an extra local
//    then control continues after the block — the binding and the
//    block local must be popped so `return total` reads the right slot.
func sumOrZero(_ x: Int?, _ base: Int) -> Int {
  var total = base
  if let n = x {
    let bonus = n + 1
    total = total + bonus
  }
  return total
}
print(sumOrZero(10, 100))   // 100 + 11 = 111
print(sumOrZero(nil, 100))  // 100

// 5. if-let on Int(_:) — the input-validation idiom this feature
//    unlocks. Early-return from each arm inside a function.
func parse(_ s: String) -> Int {
  if let n = Int(s) {
    return n
  } else {
    return -1
  }
}
print(parse("42"))
print(parse("nope"))
print("done")
