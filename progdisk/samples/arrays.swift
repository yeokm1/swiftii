// arrays.swift - array literals,
// append, subscript read/write, the
// .count and .isEmpty properties,
// and walking the elements with a
// range over the indices. Runs on
// any SwiftII REPL (lite or extras).

var xs = [10, 20, 30]
print(xs) // [10, 20, 30]
print("count \(xs.count)")

xs.append(40)
xs.append(50)
print(xs) // [10, 20, 30, 40, 50]

// Subscript write: double every
// element in place.
for i in 0..<xs.count {
  xs[i] = xs[i] * 2
}
print(xs) // [20, 40, 60, 80, 100]

// Walk the indices to sum the values
// and find the largest.
var sum = 0
var biggest = xs[0]
for i in 0..<xs.count {
  sum = sum + xs[i]
  biggest = max(biggest, xs[i])
}
print("sum \(sum)  biggest \(biggest)")

let none: [Int] = []
print("none.isEmpty = \(none.isEmpty)")
