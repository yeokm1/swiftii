// esyntax - COMPILE ERROR demo.
// Statement / declaration syntax.
// This file shows 'want )';
// the parser raises one of these for
// any token it cannot place.
//
// Other messages in this family
// (edit the source to try one; full
// list in the ERRTESTS README + the
// host test error_paths_test.c):
//   - want name
//   - want '('
//   - want ':'
//   - want '{'
//   - want ']'
//   - want '='
//   - want 'in'
//   - want 'in:'
//   - want expression
//   - want type
//   - missing '='
//   - want ';' or '}'
//   - want ';' or EOF
//   - unexpected EOF
//   - bad type ([T?])
//
// Expect on screen: compile error:
// line N: want ')'
print(1
