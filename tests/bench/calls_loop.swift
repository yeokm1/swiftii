// Phase 6b benchmark — 500 iterations of a 1-arg function call.
// Exercises OP_CALL / OP_RETURN_V / OP_GET_LOCAL / OP_SET_LOCAL on the
// function side, and OP_LOOP / OP_INC / OP_LE on the loop side.
// Phase 7 added the type tracker and full call infrastructure that the
// original Phase 6 benches (which only exercised top-level globals)
// did not measure. Without this signal, asm-handler shortlisting would
// miss OP_CALL / OP_RETURN_V if those turn out to dispatch heavily.
func inc(_ x: Int) -> Int {
  return x + 1
}

var n = 0
for i in 1...500 {
  n = inc(n)
}
print(n)
