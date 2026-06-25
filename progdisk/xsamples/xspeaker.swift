// xspeaker.swift - peek and poke:
// read a soft switch and click the
// speaker. (the `x` prefix marks a
// demo that needs the extras REPL)
//
// NEEDS THE EXTRAS REPL: when you
// run this from the file browser,
// choose [S]ATURN (II+ disk) or
// [A]UX (//e disk). The lite REPL
// has no peek/poke. (On the host
// they are no-ops, so it runs there
// silently.)
//
// Apple II soft switches are just
// memory addresses, written in
// decimal: 49200 = $C030 speaker
// toggle (each access moves the cone
// once) 49152 = $C000 keyboard data
// A poke to $C030 toggles the
// speaker, so spaced pokes are
// clicks.

print("three clicks...")
var n = 0
while n < 3 {
  // $C030 - one speaker click
  poke(49200, 0)
  var d = 0
  // spacing so the taps are distinct
  while d < 1500 {
    d = d + 1
  }
  n = n + 1
}
print("done")

// peek reads a byte back; $C000's
// high bit is set when a key is
// waiting.
print("keyboard byte = \(peek(49152))")
