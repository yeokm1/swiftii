// Phase 7 c8a: String(_ n: Int) -> String. Compiles to OP_STR_INTERP_I
// after the Int argument — the same path \(n) interpolation takes.

print(String(0))
print(String(42))
print(String(-7))
print(String(32767))

// Concat exercises the resulting heap string flowing into OP_ADD's
// polymorphic string path.
let label = "n=" + String(99)
print(label)

// Round-trip through a var.
var s = String(5)
s = s + s
print(s)
