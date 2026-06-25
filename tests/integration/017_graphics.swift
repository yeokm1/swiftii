// Phase 10 low-res graphics. On the host gr/color/plot/hlin/vlin/scrn
// are no-ops (scrn returns 0), so only the print() output is captured;
// the drawing is verified on the emulator (a green border box with a
// magenta diagonal inside).
gr()
color(1)
var i = 0
while i < 16 {
  color(i)
  plot(i, i)
  i = i + 1
}
// Border box with hlin/vlin: endpoints then the fixed line coordinate.
color(12)
hlin(0, 39, 0)
hlin(0, 39, 39)
vlin(0, 39, 0)
vlin(0, 39, 39)
text()
print("drawn \(i) blocks, corner \(scrn(0, 0))")
