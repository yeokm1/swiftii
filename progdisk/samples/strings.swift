// strings.swift - string features:
// concatenation with +, \(...)
// interpolation of Int / Bool /
// String values, and turning an Int
// into a String with String(_:).
// Runs on any SwiftII REPL (lite or
// extras).

let first = "apple"
let second = "soft"
let word = first + second
print(word) // applesoft
print("the word is \(word)")

// String(_:) renders an Int as text;
// combine with + to build a line.
var row = ""
for n in 1..<6 {
  row = row + String(n) + " "
}
print(row) // 1 2 3 4 5

// Interpolation mixes types in one
// literal.
let total = 3
let ready = true
print("total=\(total) ready=\(ready) word=\(word)")

// .count is the character length;
// .isEmpty tests for "".
print(word.count) // 9
print("".isEmpty) // true
