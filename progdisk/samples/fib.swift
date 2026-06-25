// fib.swift - first 10 Fibonacci
// numbers, iterative. (SwiftII caps
// recursion at 4 frames, so this
// uses a while loop.) Run it from
// the boot menu: option 3, open
// SAMPLES/, select FIB.SWIFT.

var a = 0
var b = 1
var i = 0
while i < 10 {
  print(a)
  let next = a + b
  a = b
  b = next
  i = i + 1
}
