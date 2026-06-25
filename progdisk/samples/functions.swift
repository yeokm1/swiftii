// functions.swift - multi-parameter
// functions, bounded recursion, and
// the min / max builtins. Runs on
// any SwiftII REPL (lite or extras).
//
// Calls are positional: the Apple II
// binaries are positional-only, so
// `power(2, 10)` rather than
// `power(of: 2, to: 10)`. (Argument
// labels are accepted only on the
// development host; see docs/using/LANGUAGE.md
// sec. Functions.)
func square(_ x: Int) -> Int {
  return x * x
}

func power(_ base: Int, _ exp: Int) -> Int {
  var result = 1
  var i = 0
  while i < exp {
    result = result * base
    i = i + 1
  }
  return result
}

// Recursion - kept shallow: SwiftII
// caps the call stack at 4 frames,
// so factorial(4) is the deepest
// this may go.
func factorial(_ n: Int) -> Int {
  if n <= 1 {
    return 1
  }
  return n * factorial(n - 1)
}

print(square(9)) // 81
print(power(2, 10)) // 1024
print(factorial(4)) // 24
// min(9, 16) -> 9
print(min(square(3), power(2, 4)))
print(max(-5, 5)) // 5
