// Compound assignment += -= *= /= on globals and locals, including the
// String += concat path. Shipped on host + the //e binaries (the II+
// binaries are budget-gated — see statements.c SWIFTII_EXT_COMPILER and
// docs/using/LANGUAGE.md). This fixture runs on the host build.
var g = 0
g += 5
g -= 2
g *= 4
g /= 3
print(g)               // ((0+5-2)*4)/3 = 4

func chain() -> Int {
  var n = 10
  n += 1
  n *= 2
  n -= 3
  n /= 2
  return n             // ((10+1)*2-3)/2 = 9
}
print(chain())

var s = "a"
s += "bc"
print(s.count)         // 3
