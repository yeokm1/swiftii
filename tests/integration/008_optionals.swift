// Phase 4 commit 4 — optionals demo: type annotation, nil literal,
// force-unwrap, nil-coalescing, if let.
let a: Int? = 5
print(a!)               // 5

let b: Int? = nil
print(b ?? 99)          // 99 (fallback)
print(a ?? 99)          // 5 (no fallback needed)

// Chained ??: left-associative — both behave the same for binary form.
let c: Int? = nil
print(c ?? a ?? 100)    // 5

// if let — top-level binding (Phase 4 limitation, no `else`).
if let v = a {
  print("some")
  print(v + 1)
}
if let v = b {
  print("never printed")
}
print("done")
