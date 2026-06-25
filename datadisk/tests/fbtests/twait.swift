// twait.swift - wait(_ ms:) delay.
// FAMILY B (compiler-runner) ONLY:
// wait() is a program builtin, so no
// REPL ships it - boot a compiler
// disk, press [X]. The middle
// section pauses for 1, then 2, then
// 3 seconds - rising gaps so the
// delay is easy to see, and you can
// eyeball that each pause is ~its
// stated length. On host wait() is a
// no-op (instant). Last line
// "fail 0" = pass.

var npass = 0
var nfail = 0
func chk(_ g: Int, _ w: Int) {
  if g == w { npass = npass + 1 }
  else {
    nfail = nfail + 1
    print("FAIL got \(g) want \(w)")
  }
}

// 1) zero delay returns immediately.
var x = 41
wait(0)
x = x + 1
chk(x, 42)

// 2) VISIBLE: each line says how
//    long it then pauses - 1, then
//    2, then 3 seconds. The rising,
//    different gaps make the delay
//    obvious on target (and show
//    wait() honors its argument).
var s = 1
while s <= 3 {
  print("wait \(s) s ...")
  wait(s * 1000)
  s = s + 1
}
print("done")
chk(s, 4)

// 3) state survives delayed loops.
var sum = 0
var i = 0
while i < 5 {
  wait(50)
  sum = sum + i
  i = i + 1
}
chk(sum, 10)

print("pass \(npass) fail \(nfail)")
