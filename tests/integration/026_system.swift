// System + math/string builtins — all Family-B-only. wait()/tone() are no-ops
// on the host (so output stays deterministic); on the Apple II Runner wait()
// loops the monitor ROM WAIT and tone() toggles the speaker, but execution
// carries on either way. abs/sgn/hasPrefix/hasSuffix return real values and are
// asserted below.
//   wait(_ ms: Int)                       — busy-wait roughly `ms` ms
//   tone(_ halfPeriod: Int, _ cycles: Int) — square-wave speaker tone
//   abs(_ x: Int) -> Int                   — |x|
//   sgn(_ x: Int) -> Int                   — -1 / 0 / 1
//   s.hasPrefix(_ t: String) -> Bool       — starts-with
//   s.hasSuffix(_ t: String) -> Bool       — ends-with
print("before")
wait(50)
tone(40, 100)
print("after")
print(abs(0 - 7))
print(abs(9))
print(sgn(0 - 4))
print(sgn(0))
print(sgn(12))
print("hello.swift".hasSuffix(".swift"))
print("hello".hasPrefix("he"))
print("hello".hasPrefix("xy"))
