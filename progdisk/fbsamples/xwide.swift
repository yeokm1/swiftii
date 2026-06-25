// xwide.swift - an 80-column number-guessing game for the II+ Videx
// Videoterm and the //e aux 80-column display. text80() switches to 80
// cols so the 1-80 ruler fits one row; on a 40-col machine it is a no-op
// and the ruler wraps. No text() at the end (that clears+reverts) - the
// Runner reverts on exit, so the result stays visible.
//
// FAMILY B ONLY (random(in:)): run with [X]; real 80 cols needs a Saturn
// 128K + Videoterm, or a //e with aux RAM. The ruler is a one-row literal
// (a string-built bar would exhaust the heap). random(in:) is seeded from
// keypress timing (the "press RETURN to start" - like Applesoft's RND), so
// the target differs each game; the host has no such entropy, so
// ./swiftii_host always picks the same.

text80()
print("=========================== 80-COLUMN NUMBER HUNT ============================")
print("I picked a number from 1 to 80. Use the ruler to home in - you get 6 guesses.")
print("ruler 1->  10        20        30        40        50        60        70    80")
print("           |         |         |         |         |         |         |     |")

// Seed the RNG from how long you take to press RETURN (see the header note).
print("")
print("press RETURN to start:")
let _ = readLine()

let target = random(in: 1...80)
var tries = 0
var won = false

while tries < 6 {
  print("")
  print("guess (1-80):")
  if let line = readLine() {
    if let g = Int(line) {
      tries = tries + 1
      if g == target {
        print("CORRECT - it was \(target), in \(tries) guesses!")
        won = true
        tries = 6
      } else {
        if g < target {
          print("\(g) is too LOW  - aim higher (to the right on the ruler)")
        } else {
          print("\(g) is too HIGH - aim lower (to the left on the ruler)")
        }
      }
    } else {
      print("that is not a number")
    }
  }
}

if !won {
  print("out of guesses - the number was \(target)")
}
print("nice game! (this 80-column result stays up until you continue)")
