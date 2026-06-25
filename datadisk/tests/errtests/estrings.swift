// estrings - COMPILE ERROR demo.
// Strings and interpolation. This
// file shows `unknown string escape
// sequence` (the \q below).
//
// Other messages in this family
// (edit the source to try one; full
// list in the ERRTESTS README + the
// host test error_paths_test.c):
//   - empty interpolation
//   - bad interp
//   - nested string literal not
//     supported
//   - newline in string literal
//   - unterminated string literal
//   - unterminated interpolation in
//     string literal
//
// Expect on screen: compile error:
// line N: unknown string escape
// sequence
print("\q")
