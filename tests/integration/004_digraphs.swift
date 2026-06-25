// Phase 3 (rev 3 typing model): canonical lowercase Swift exercising
// the characters the //+ keyboard cannot type directly — `{ }`, `_`,
// `\` — which on a real //+ would be authored via the input layer
// (`<%`, `%>`, Ctrl-W, `??/`). The host and the //e see this file
// byte-for-byte; the //+ produces the same canonical file from typed
// uppercase + digraphs (see tests/unit/input_translate_test.c).
var sum_1 = 0
for i in 1...5 {
  sum_1 = sum_1 + i
}
print("sum is \(sum_1)")
print("a\nb")
