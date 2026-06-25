// Phase 10 platform builtins (SWIFTSAT/host). On the host peek returns
// 0 (no raw memory access) and poke is a no-op, so the output is
// deterministic. home() is exercised by the unit test (its host output
// is an ANSI clear-screen escape) and is left out here to keep the
// expected output plain.
poke(49200, 0)
let v = peek(1024)
print(v)
print(peek(49200))
// Cursor positioning (slice 2) — no-op on host, so output is unchanged.
vtab(2)
htab(3)
print(peek(49200))
