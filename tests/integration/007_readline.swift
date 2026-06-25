// Phase 4 readLine demo: echo each line until EOF, terminate on
// empty input. Combines readLine, nil-comparison, break, and the
// terminator overload for an inline prompt.
while true {
  print("> ", terminator: "")
  let line = readLine()
  if line == nil { break }
  print("got: ", terminator: "")
  print(line)
}
print("bye")
