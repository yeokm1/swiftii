// Phase 9 stage 1 commit 3b: asc(_ s: String) -> Int.
// First real XLC-resident built-in. On the host (this test runs
// against build/host/swiftii_host) the worker lives in normal CODE;
// on SWIFTSAT it lives in Saturn bank 1 and reaches it via the
// call_xlc_asc trampoline. Identical semantics either way.

print(asc("A"))           // 65
print(asc("z"))           // 122
print(asc("hello"))       // 104 — only the first byte
print(asc("0"))           // 48

// Composes with String() round-trip — verify the ASCII byte of the
// first character of a decimal-formatted Int.
print(asc(String(7)))     // 55  ('7')

// Inside a function. Threads through OP_CALL_BUILTIN inside a
// callee frame, so the locals/stack accounting must stay balanced.
func firstByte(_ s: String) -> Int {
  return asc(s)
}
print(firstByte("Swift")) // 83  ('S')
