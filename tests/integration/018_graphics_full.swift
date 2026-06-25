// Phase 10 slice 3 (full-screen variant). grFull() is 40x48 (no text
// window), so plot reaches y=47. Host no-ops the drawing; the diagonal
// of colored blocks is verified on the emulator.
grFull()
var i = 0
while i < 48 {
  color(i % 16)
  plot(i % 40, i)
  i = i + 1
}
text()
print("full \(i)")
