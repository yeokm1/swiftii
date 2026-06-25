// xfuncs.swift — function-heavy
// demo: 20 functions, total
// bytecode exceeds the flat 1,834 B
// Compiler cap, so it compiles +
// runs only via a paged Compiler:
// Tier 2 Saturn or Tier 3 //e aux.
// Each fK returns K (the filler
// statements just bulk up the
// arena); the driver sums f1..f20 =
// 210. Calls are POSITIONAL (fK(0))
// — cc65 builds don't take
// call-site argument labels (fK(n:
// 0)).
func f1(n: Int) -> Int {
  var a = n
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  return 1
}
func f2(n: Int) -> Int {
  var a = n
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  return 2
}
func f3(n: Int) -> Int {
  var a = n
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  return 3
}
func f4(n: Int) -> Int {
  var a = n
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  return 4
}
func f5(n: Int) -> Int {
  var a = n
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  return 5
}
func f6(n: Int) -> Int {
  var a = n
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  return 6
}
func f7(n: Int) -> Int {
  var a = n
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  return 7
}
func f8(n: Int) -> Int {
  var a = n
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  return 8
}
func f9(n: Int) -> Int {
  var a = n
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  return 9
}
func f10(n: Int) -> Int {
  var a = n
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  return 10
}
func f11(n: Int) -> Int {
  var a = n
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  return 11
}
func f12(n: Int) -> Int {
  var a = n
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  return 12
}
func f13(n: Int) -> Int {
  var a = n
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  return 13
}
func f14(n: Int) -> Int {
  var a = n
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  return 14
}
func f15(n: Int) -> Int {
  var a = n
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  return 15
}
func f16(n: Int) -> Int {
  var a = n
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  return 16
}
func f17(n: Int) -> Int {
  var a = n
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  return 17
}
func f18(n: Int) -> Int {
  var a = n
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  return 18
}
func f19(n: Int) -> Int {
  var a = n
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  return 19
}
func f20(n: Int) -> Int {
  var a = n
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  a = a + 1
  return 20
}
var s = 0
s = s + f1(0)
s = s + f2(0)
s = s + f3(0)
s = s + f4(0)
s = s + f5(0)
s = s + f6(0)
s = s + f7(0)
s = s + f8(0)
s = s + f9(0)
s = s + f10(0)
s = s + f11(0)
s = s + f12(0)
s = s + f13(0)
s = s + f14(0)
s = s + f15(0)
s = s + f16(0)
s = s + f17(0)
s = s + f18(0)
s = s + f19(0)
s = s + f20(0)
print(s)
