// enamelen.swift - COMPILE ERROR
// demo: an identifier longer than
// the 11-char symbol-table limit
// (IDENT_MAX-1 = 11). The compiler
// rejects an over-length name
// instead of silently truncating it
// -- truncation would let two names
// that share the same first 11
// characters collide on one slot.
// The name below, longvarname1, is
// 12 characters.
//
// Expected on screen: compile error:
// line 19: name >11 chars
//
// (An 11-char name like longvarnam1
// compiles fine. The same dedicated
// message fires for let/var,
// function names, parameters, for-in
// loop variables, and if-let
// bindings.)
let longvarname1 = 1
