// elex - COMPILE ERROR demo.
// Lexer (non-string). This file
// shows `integer literal out of Int
// range`.
//
// Other messages in this family
// (edit the source to try one; full
// list in the ERRTESTS README + the
// host test error_paths_test.c):
//   - bare '&' is not an operator
//   - bare '|' is not an operator
//   - unexpected character
//
// Expect on screen: compile error:
// line N: integer literal out of Int
// range
let x = 99999999999
