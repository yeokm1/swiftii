// for-in over an array (Phase 16 stretch — Family B / host only).
// Element walk; the loop var binds each element in order.

let xs = [10, 20, 30]
var sum = 0
for v in xs {
  sum = sum + v
}
print(sum)            // 60

for v in xs {
  print(v)            // 10 / 20 / 30
}

let names = ["ann", "bob", "cy"]
for n in names {
  print(n)            // ann / bob / cy
}

// break inside the walk
var hit = 0
for v in xs {
  hit = hit + 1
  if v == 20 { break }
}
print(hit)            // 2

// empty array: body never runs
let empty: [Int] = []
for v in empty {
  print(v)
}
print(0)              // 0

// in a function, with a body local and nested loops
func total(_ a: [Int]) -> Int {
  var s = 0
  for v in a {
    let doubled = v * 2
    s = s + doubled
  }
  return s
}
print(total([1, 2, 3]))   // 12

func grid() -> Int {
  let rows = [10, 20]
  let cols = [1, 2, 3]
  var acc = 0
  for r in rows {
    for c in cols {
      acc = acc + r + c
    }
  }
  return acc
}
print(grid())             // 102

// early return out of the loop (must not leak the array/index)
func firstBig(_ xs: [Int]) -> Int {
  for v in xs {
    if v > 5 { return v }
  }
  return -1
}
print(firstBig([1, 4, 9, 16]))   // 9
