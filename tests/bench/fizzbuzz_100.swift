// Phase 6 benchmark — FizzBuzz 1..100.
// Exercises: integer arith (%, ==), branching (if / else if), int->str
// conversion in print, builtin call overhead, tight for-loop dispatch.
for i in 1...100 {
  if i % 15 == 0 { print("FizzBuzz") }
  else if i % 3 == 0 { print("Fizz") }
  else if i % 5 == 0 { print("Buzz") }
  else { print(i) }
}
