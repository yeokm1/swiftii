// greet.swift - interactive input
// with readLine(). Runs on ANY REPL.
//
// readLine() returns the typed line
// as String? (nil at end-of-input);
// `??` supplies a fallback, and
// print(terminator: "") keeps the
// prompt on the same line as the
// answer.

print("what's your name? ", terminator: "")
let name = readLine() ?? "friend"
print("hello, \(name)!")
print("(that name has \(name.count) letters)")
