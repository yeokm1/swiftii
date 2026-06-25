// Phase 6 benchmark — sum of 1..1000.
// Exercises: tight integer loop, OP_GET_LOCAL / OP_SET_LOCAL for the
// accumulator, OP_ADD on T_INT, OP_LOOP back-edge, no allocations.
// The cleanest profile signal for the VM dispatch hot path.
var total = 0
for i in 1...1000 {
  total = total + i
}
print(total)
