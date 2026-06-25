// Phase 9 stage 1: Int(_ s: String) -> Int? — the Phase 10 blocker.
// XLC-resident builtin (Saturn bank 1 on SWIFTSAT, normal CODE on
// host). Returns nil on empty / non-numeric / overflow input.

// Valid parses.
print(Int("42")!)          // 42
print(Int("-7")!)          // -7
print(Int("+5")!)          // 5  (leading + accepted, like Swift)
print(Int("0")!)           // 0
print(Int("32767")!)       // 32767  (int16 max)
print(Int("-32768")!)      // -32768 (int16 min)

// Failed parses become nil; exercise via ?? and if let.
print(Int("nope") ?? -1)   // -1
print(Int("") ?? -1)       // -1   (empty)
print(Int("12x") ?? -1)    // -1   (trailing junk)
print(Int("32768") ?? -1)  // -1   (overflow past int16 max)

if let n = Int("100") {
  print(n + 1)             // 101
}
// The nil path is covered by the `?? -1` cases above; if-let/else has its
// own integration coverage.

// Round-trips with String(): parse a formatted Int back.
print(Int(String(123))!)   // 123

// Composes with readLine()! — the canonical Phase 10 idiom — is the
// REPL/emulator test; here we keep it deterministic with literals.
