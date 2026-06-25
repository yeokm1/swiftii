// ttone.swift - tone(_ halfPeriod:_
// cycles:) square-wave speaker tone.
// FAMILY B (compiler-runner) ONLY:
// tone() is a program builtin, so no
// REPL ships it - boot a compiler
// disk, press [X]. The middle
// section plays five RISING blips,
// then a low->high chirp - LISTEN
// for them (smaller halfPeriod =
// higher note). On host tone() is a
// no-op (silent); some emulators
// don't model the $C030 speaker, so
// a real //e / II+ is the true audio
// test. Last line "fail 0" = pass.

var npass = 0
var nfail = 0
func chk(_ g: Int, _ w: Int) {
  if g == w { npass = npass + 1 }
  else {
    nfail = nfail + 1
    print("FAIL got \(g) want \(w)")
  }
}

// 1) tone() runs and returns; zero
//    cycles is silent + immediate.
var x = 41
tone(40, 0)
x = x + 1
chk(x, 42)

// 2) AUDIBLE: five rising blips,
//    then a low->high chirp. Listen,
//    and eyeball that all five ran.
var n = 0
let halves = [120, 90, 60, 45, 30]
for h in halves {
  print("blip \(n + 1) ...")
  tone(h, 120)
  wait(120)
  n = n + 1
}
chk(n, 5)
print("chirp ...")
tone(80, 60)
tone(35, 90)

// 3) state survives tone in a loop.
var sum = 0
var i = 0
while i < 5 {
  tone(60, 20)
  sum = sum + i
  i = i + 1
}
chk(sum, 10)

print("pass \(npass) fail \(nfail)")
