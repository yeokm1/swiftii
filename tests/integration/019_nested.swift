// Nested loops / nested ifs — regression test for the cc65 -Cl
// static-local clobber (docs/contributing/LESSONS.md 2026-06-03). On the buggy compiler
// the outer loop ran exactly once and an `if`-no-else with a nested
// `if` mis-jumped; the recursion-safe placeholder LIFO fixes both.

// for-in containing a for-in: the outer must run all 3 times.
var grid = 0
for i in 1...3 {
  for j in 1...3 {
    grid = grid + 1
  }
}
print(grid)          // 9

// while containing a while.
var w = 0
var outer = 0
while outer < 3 {
  var inner = 0
  while inner < 2 {
    w = w + 1
    inner = inner + 1
  }
  outer = outer + 1
}
print(w)             // 6

// for-in containing a while.
var mix = 0
for k in 1...4 {
  var c = 0
  while c < k {
    mix = mix + 1
    c = c + 1
  }
}
print(mix)           // 1+2+3+4 = 10

// if (no else) whose body holds a nested if — both arms.
func classify(_ n: Int) -> Int {
  if n > 0 {
    if n > 10 {
      return 2
    }
    return 1
  }
  return 0
}
print(classify(5))   // 1
print(classify(50))  // 2
print(classify(-1))  // 0

// Nested loop INSIDE an if body, and an if INSIDE a loop body.
var s = 0
if grid == 9 {
  for a in 1...3 {
    if a == 2 {
      s = s + 100
    }
    s = s + a
  }
}
print(s)             // (1+2+3) + 100 = 106
