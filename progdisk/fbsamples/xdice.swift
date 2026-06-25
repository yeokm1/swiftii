// xdice.swift - a tour of the Phase
// 16 Family-B features: random(in:)
// - dice rolls from a fixed-seed RNG
// switch - name notable two-dice
// sums for-in array - walk a
// histogram to draw bars FAMILY B
// ONLY. Run with [X] on a compiler
// disk (the REPLs reject all three;
// that's the deliberate dialect
// fork). Host: ./swiftii_host
// xdice.swift.

print("rolling 2d6 a dozen times")

// hist[s] counts how often the two
// dice summed to s (s in 2...12).
var hist = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]

var rolls = 0
while rolls < 12 {
  let a = random(in: 1...6)
  let b = random(in: 1...6)
  let sum = a + b
  hist[sum] = hist[sum] + 1

  // switch names the famous craps
  // sums; default covers the rest.
  var name = "-"
  switch sum {
  case 2: name = "snake eyes"
  case 7: name = "lucky seven"
  case 11: name = "yo-eleven"
  case 12: name = "boxcars"
  default: name = "-"
  }
  print("\(a)+\(b) = \(sum)  \(name)")
  rolls = rolls + 1
}

// for-in over the histogram array,
// drawing one '#' per occurrence.
print("distribution:")
var face = 0
for count in hist {
  if face >= 2 {
    var bar = ""
    var k = 0
    while k < count {
      bar = bar + "#"
      k = k + 1
    }
    print("\(face): \(bar)")
  }
  face = face + 1
}
