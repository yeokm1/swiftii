// Bool as a type: literals, printing, prefix `!`, the comparison
// operators (each yields a Bool), `&&`/`||`, an annotated Bool variable
// that is reassigned, a Bool used as an `if` condition, and a function
// with a Bool parameter and Bool return.

print(true)            // true
print(false)           // false
print(!true)           // false
print(!false)          // true

// every comparison operator produces a Bool
print(1 == 1)          // true
print(1 != 1)          // false
print(2 < 3)           // true
print(3 <= 3)          // true
print(4 > 5)           // false
print(5 >= 5)          // true

// logical operators on Bool
print(true && false)   // false
print(true || false)   // true
print(!(1 < 2))        // false

// annotated Bool variable, then reassigned
var ready: Bool = false
print(ready)           // false
ready = true
print(ready)           // true

// a Bool drives an if condition
if ready {
  print("go")          // go
}

// Bool parameter + Bool return
func negate(_ b: Bool) -> Bool {
  return !b
}
print(negate(true))    // false
print(negate(false))   // true
