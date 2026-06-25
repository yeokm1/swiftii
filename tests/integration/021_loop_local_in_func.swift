// Regression: a `for-in` containing a `while` whose body declares a local
// `var` used to return wrong results INSIDE a function — the for-in keeps
// range_end on the value stack, which shifted the body local's slot so it
// aliased range_end (the while never ran). Fixed by reserving an anonymous
// slot for range_end in a function body (statements.c). The top-level form
// (019_nested) was always correct. Host + //e; II+ is budget-gated.

// for-in + nested while + body-local var, all inside a function.
func triangle(_ n: Int) -> Int {
  var m = 0
  for k in 1...n {
    var c = 0
    while c < k { m = m + 1; c = c + 1 }
  }
  return m
}
print(triangle(4))     // 1+2+3+4 = 10

// for-in whose body local is read after the loop var: must not alias.
func sumPlusOne(_ n: Int) -> Int {
  var t = 0
  for i in 0..<n {
    var one = 1
    t = t + i + one
  }
  return t
}
print(sumPlusOne(4))   // (0+1)+(1+1)+(2+1)+(3+1) = 10

// Nested for-in inside a function (each keeps its own range_end).
func grid(_ n: Int) -> Int {
  var g = 0
  for x in 1...n {
    for y in 1...n { g = g + 1 }
  }
  return g
}
print(grid(3))         // 9
