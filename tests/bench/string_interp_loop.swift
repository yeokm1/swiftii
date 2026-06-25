// Phase 6b benchmark — 100 iterations of String() conversion + concat.
// Exercises OP_STR_INTERP_I (the path Phase 7 c8a's String(_ n: Int)
// compiles to), OP_STR_CONCAT, and the heap_alloc / refcount opcodes
// that string-heavy programs hit on every loop. The original Phase 6
// benches were integer-only; without this signal the asm-handler
// shortlist could underweight string-path opcodes that Phase 7
// surfaced.
var s = ""
for i in 1...100 {
  s = "n=" + String(i)
}
print(s)
