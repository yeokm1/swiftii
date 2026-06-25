// efuncs - COMPILE ERROR demo.
// Functions. This file shows
// `missing return` (an Int func
// with no value on a path).
//
// Other messages in this family
// (edit the source to try one; full
// list in the ERRTESTS README + the
// host test error_paths_test.c):
//   - void no value
//   - bad arg count
//   - use positional args, not
//     labels
//   - no nested func
//
// Expect on screen: compile error:
// line N: missing return
func g() -> Int { }
