// Phase 4 demo: break + print(_:terminator:).
print("count: ", terminator: "")
var i = 0
while true {
  i = i + 1
  if i > 3 { break }
  print(i, terminator: " ")
}
print("done")

// Multiple breaks in one loop (loops.c supports up to 4 per body).
for n in 1...10 {
  if n == 4 { break }
  if n == 7 { break }
  print(n)
}
