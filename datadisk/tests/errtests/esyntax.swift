// esyntax - COMPILE ERROR demo.
// Statement / declaration syntax.
// This file shows 'expected )';
// the parser raises one of these for
// any token it cannot place.
//
// Other messages in this family
// (edit the source to try one; full
// list in the ERRTESTS README + the
// host test error_paths_test.c):
//   - expected name
//   - expected '('
//   - expected ':'
//   - expected '{'
//   - expected ']'
//   - expected '='
//   - expected 'in'
//   - expected 'in:'
//   - expected expression
//   - expected type
//   - missing '='
//   - expected ';' or '}'
//   - expected ';' or EOF
//   - unexpected EOF
//   - unsupported type ([T?])
//
// Expect on screen: compile error:
// line N: expected ')'
print(1
