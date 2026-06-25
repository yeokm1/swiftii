// xgraphics.swift - low-resolution
// colour graphics (GR mode). (the
// `x` prefix marks a demo that needs
// the extras REPL)
//
// NEEDS THE EXTRAS REPL: when you
// run this from the file browser,
// choose [S]ATURN (II+ disk) or
// [A]UX (//e disk). The lite REPL
// has no graphics builtins, so it
// cannot run this. (On the host the
// GR builtins are no-ops, so it runs
// there but draws nothing.)

// mixed GR: 40x40 grid + text window
gr()

// Draw a white border around the
// 40x40 grid.
color(15) // white
hlin(0, 39, 0) // top edge
hlin(0, 39, 39) // bottom edge
vlin(0, 39, 0) // left edge
vlin(0, 39, 39) // right edge

// A magenta diagonal across the
// grid.
color(1) // magenta
var i = 0
while i < 40 {
  plot(i, i)
  i = i + 1
}

// Read one cell back with scrn(_:_:)
// and report it in the text window.
print("corner colour = \(scrn(0, 0))")
print("press return")
let _ = readLine()

text() // back to 40-column text
