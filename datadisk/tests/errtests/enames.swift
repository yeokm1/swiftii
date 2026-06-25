// enames - COMPILE ERROR demo.
// Name resolution and typing. This
// file shows 'undeclared name'. (An
// over-length name is its own demo,
// ENAMELEN.)
//
// Other messages in this family
// (edit the source to try one; full
// list in the ERRTESTS README + the
// host test error_paths_test.c):
//   - type mismatch
//   - unknown member
//   - need '(...)'
//   - name longer than 11 chars (see
//     ENAMELEN)
//
// Expect on screen: compile error:
// line N: undeclared name
print(zz)
