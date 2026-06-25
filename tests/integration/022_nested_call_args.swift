// Regression (2026-06-06): a user-function call used as an ARGUMENT to
// another user-function call. parse_call_arglist_emit is re-entrant, and
// under cc65 -Cl its `argc` (and `fn_idx`) are static, so the inner call
// clobbered the outer's running count and the outer failed to compile as
// `bad arg count` on the Apple II target (host auto-locals were fine).
// Fixed by parking argc + fn_idx on the patch_stack across the recursion.
// The on-disk suite (datadisk/tests/tfunc, tcontrol, toptional, tassign)
// is full of `chk(userfunc(...), want)`, which is exactly this pattern.

func id(_ x: Int) -> Int { return x }
func add(_ a: Int, _ b: Int) -> Int { return a + b }
func one() -> Int { return 1 }

print(add(id(3), 4))        // 7  — 1-arg call nested in a 2-arg call's args
print(add(one(), one()))    // 2  — two 0-arg calls as args
print(id(add(2, id(5))))    // 7  — nested both ways
print(add(id(id(9)), 0))    // 9  — call nested two deep
print(id(id(id(8))))        // 8
