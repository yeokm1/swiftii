// Phase 4 functions demo: declarations, locals, recursion, and a
// function that calls another. Calls are positional — the Apple II
// binaries are positional-only (argument labels are a host-dev nicety;
// see docs/using/LANGUAGE.md § Functions).
func sq(_ x: Int) -> Int {
  return x * x
}

func add(a: Int, b: Int) -> Int {
  let s = a + b
  return s
}

// fact(4) needs exactly VM_CALL_FRAMES=4 stack frames at deepest;
// fact(5) would overflow (Phase 4 call-depth cap is 4 per
// docs/using/LANGUAGE.md § Implementation limits).
func fact(_ n: Int) -> Int {
  if n <= 1 {
    return 1
  }
  return n * fact(n - 1)
}

func greet() {
  print("hi")
}

greet()
print(sq(7))
print(add(3, 4))
print(fact(4))
print(sq(add(1, 2)))
