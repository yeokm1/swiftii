// Phase 9 stage 1 commit 4: chr(_ n: Int) -> String.
// Inverse of asc (test 011) and the second XLC-resident built-in.
// On the host (this test runs against build/host/swiftii_host) the
// worker lives in normal CODE; on SWIFTSAT it lives in Saturn bank 1
// and reaches it via the same generic call_xlc_dispatch trampoline.
// Identical semantics either way.

print(chr(65))            // A
print(chr(122))           // z
print(chr(48))            // 0

// Round-trips with asc — the first byte of "Swift" back to a string.
print(chr(asc("Swift")))  // S

// Composes with string concatenation.
print(chr(72) + chr(105)) // Hi

// Inside a function. Threads through OP_CALL_BUILTIN inside a callee
// frame, so the locals/stack accounting must stay balanced.
func digit(_ n: Int) -> String {
  return chr(asc("0") + n)
}
print(digit(7))           // 7
