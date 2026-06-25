// xcolors.swift - all 16 low-res
// (GR) colours as vertical bands.
// (the `x` prefix marks a demo that
// needs the extras REPL)
//
// NEEDS THE EXTRAS REPL: when you
// run this from the file browser,
// choose [S]ATURN (II+ disk) or
// [A]UX (//e disk). The lite REPL
// has no graphics builtins, so it
// cannot run this. (On the host the
// GR builtins are no-ops, so it runs
// there but draws nothing.)

gr()

// Walk the 40 columns. color(x % 16)
// cycles all 16 lo-res colours, and
// vlin fills each column top-to-
// bottom into a vertical band.
var x = 0
while x < 40 {
  color(x % 16)  // cycle 16 colours
  vlin(0, 39, x) // fill the column
  x = x + 1
}

print("the 16 lo-res colours")
print("press return")
let _ = readLine()

text() // back to 40-column text
