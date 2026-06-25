// eruntime - RUNTIME ERROR demo.
// This COMPILES cleanly; the Runner
// traps while executing it. The REPL
// shows `runtime error` for all of
// these (the underlying SE_* code
// differs).
//
// This file: division by zero. Other
// runtime traps:
//   - array index out of bounds (see
//     EBOUNDS)
//   - force-unwrap of a nil optional
//   - removeLast() on an empty array
//   - stack overflow (unbounded
//     recursion)
//
// Expect on screen (after loading):
// runtime error
var d = 0
print(100 / d)
