// Phase 3: strings, concat, interpolation, escapes.
let name = "Woz"
print("Hello, " + name + "!")
let n = 42
print("answer: \(n)")
print("line1\nline2")

// Phase 13: String .count / .isEmpty (Tier 1, previously crashed the VM).
// Covers a literal, a pool-resident binding, a heap string (concat
// result), and the empty string.
print("Woz".count)          // 3
print(name.count)           // 3
print(name.isEmpty)         // false
print("".isEmpty)           // true
let greeting = name + "!"    // heap-allocated string
print(greeting.count)       // 4
print(greeting.isEmpty)     // false
