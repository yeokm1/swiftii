// fizzbuzz.swift - the classic, 1
// through 15. Run it from the boot
// menu: option 3, open SAMPLES/,
// pick FIZZBUZZ.SWIFT.

var i = 1
while i <= 15 {
  if i % 15 == 0 {
    print("fizzbuzz")
  } else if i % 3 == 0 {
    print("fizz")
  } else if i % 5 == 0 {
    print("buzz")
  } else {
    print(i)
  }
  i = i + 1
}
