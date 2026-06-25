// random(in:) — Phase 16 stretch, Family B / host only.
// Fixed-seed 16-bit LCG, so the sequence is deterministic (testable).

// closed range 1...6 (dice): first eight rolls
for i in 0..<8 {
  print(random(in: 1...6))
}

// half-open 0..<2 (coin): 200 flips, all counted, split tracked
var c0 = 0
var c1 = 0
for i in 0..<200 {
  let b = random(in: 0..<2)
  if b == 0 { c0 = c0 + 1 }
  if b == 1 { c1 = c1 + 1 }
}
print(c0 + c1)     // 200 (every flip landed 0 or 1)

// 500 dice rolls all fall inside 1...6
var bad = 0
for i in 0..<500 {
  let d = random(in: 1...6)
  if d < 1 { bad = bad + 1 }
  if d > 6 { bad = bad + 1 }
}
print(bad)         // 0

// negative bounds work too
for i in 0..<4 {
  print(random(in: -3..<0))   // each in {-3,-2,-1}
}
