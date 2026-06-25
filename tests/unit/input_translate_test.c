/* Input-layer translation tests (design doc 003 revision 3).
 *
 * Exercises `input_translate` — the //+ typing-model state machine
 * that rewrites typed bytes (uppercase letters + apostrophe case
 * markers + Ctrl-W + C-standard digraphs) into canonical lowercase
 * ASCII. The translator is portable C; running it under clang on
 * the host catches the algorithmic bugs that would otherwise only
 * surface when typing into a hardware //+ at REPL latency.
 */

#include <stdint.h>
#include <string.h>

#include "platform/apple2/input.h"

#define EXPECT(cond, code) do { if (!(cond)) return (code); } while (0)

static int run(const char *in, const char *expect) {
  char buf[128];
  uint16_t in_len;
  uint16_t expect_len;
  uint16_t out_len;

  in_len = (uint16_t)strlen(in);
  expect_len = (uint16_t)strlen(expect);
  out_len = input_translate(in, in_len, buf, (uint16_t)sizeof(buf));
  if (out_len != expect_len) return 1;
  if (memcmp(buf, expect, expect_len) != 0) return 2;
  return 0;
}

/* Canonical -> input_untranslate -> input_translate must reproduce the
 * canonical line (the editor load/save round-trip for the //+ build). */
static int roundtrip(const char *canon) {
  char inform[256];
  char back[256];
  uint16_t clen = (uint16_t)strlen(canon);
  uint16_t ilen = input_untranslate(canon, clen, inform, (uint16_t)sizeof inform);
  uint16_t blen = input_translate(inform, ilen, back, (uint16_t)sizeof back);
  if (blen != clen) return 1;
  if (memcmp(back, canon, clen) != 0) return 2;
  return 0;
}

int test_input_untranslate_roundtrip(void) {
  /* The greet.swift bug: a capital in an identifier must survive. */
  EXPECT(roundtrip("let name = readLine() ?? \"friend\"") == 0, 1);
  EXPECT(roundtrip("print(\"hello, \\(name)!\")") == 0, 2);
  EXPECT(roundtrip("print(\"what's your name? \", terminator: \"\")") == 0, 3);
  /* Capitalised identifiers / function + type names. */
  EXPECT(roundtrip("func sqVal(n: Int) -> Int { return n * n }") == 0, 4);
  EXPECT(roundtrip("var myXs = [1, 2, 3]") == 0, 5);
  /* Capital at the start of a string (not after a letter). */
  EXPECT(roundtrip("print(\"Hello World\")") == 0, 6);
  /* All-lowercase line is unchanged. */
  EXPECT(roundtrip("for i in 0..<count { total = total + i }") == 0, 7);
  return 0;
}

int test_input_auto_lowercase(void) {
  /* Bare letters auto-lowercase across the alphabet. */
  EXPECT(run("LET X = 5", "let x = 5") == 0, 1);
  EXPECT(run("PRINT(HELLO)", "print(hello)") == 0, 2);
  /* Digits and punctuation untouched. */
  EXPECT(run("123 + 456", "123 + 456") == 0, 3);
  return 0;
}

int test_input_single_case_marker(void) {
  /* `'` before a letter uppercases just that letter. */
  EXPECT(run("'INT", "Int") == 0, 1);
  EXPECT(run("LET X: 'INT = 5", "let x: Int = 5") == 0, 2);
  /* Marker dies on a non-letter; the letter after is auto-lowercased. */
  EXPECT(run("'5X", "5x") == 0, 3);
  return 0;
}

int test_input_run_case_marker(void) {
  /* `''` runs until first non-letter. */
  EXPECT(run("''MAX + 1", "MAX + 1") == 0, 1);
  /* Run stops at the space; `d` is auto-lowercased like any letter. */
  EXPECT(run("''ABC d", "ABC d") == 0, 2);
  /* Multiple runs interleave with bare letters. */
  EXPECT(run("''A B ''C", "A b C") == 0, 3);
  return 0;
}

int test_input_ctrl_w_underscore(void) {
  /* Ctrl-W ($17) emits `_` (the II+ underscore key, shared with the editor). */
  EXPECT(run("\x17INTERNAL", "_internal") == 0, 1);
  EXPECT(run("SUM\x17ONE", "sum_one") == 0, 2);
  EXPECT(run("LET \x17X = 0", "let _x = 0") == 0, 3);
  return 0;
}

int test_input_digraphs(void) {
  /* C-standard digraphs translate to `{ } [ ] \ |`. */
  EXPECT(run("<%", "{") == 0, 1);
  EXPECT(run("%>", "}") == 0, 2);
  EXPECT(run("<:", "[") == 0, 3);
  EXPECT(run(":>", "]") == 0, 4);
  EXPECT(run("?\?/", "\\") == 0, 5);
  EXPECT(run("?\?!", "|") == 0, 6);
  /* Combined: array literal, block, backslash. */
  EXPECT(run("VAR XS = <:1, 2, 3:>", "var xs = [1, 2, 3]") == 0, 7);
  EXPECT(run("IF X > 0 <% PRINT(X) %>",
             "if x > 0 { print(x) }") == 0, 8);
  return 0;
}

int test_input_string_apostrophe_contextual(void) {
  /* Inside a string: between letters = literal; after non-letter =
   * case marker. */
  EXPECT(run("\"DON'T\"", "\"don't\"") == 0, 1);
  EXPECT(run("\"'HELLO\"", "\"Hello\"") == 0, 2);
  EXPECT(run("\"'ABC 'D\"", "\"Abc D\"") == 0, 3);
  /* Apostrophe right after the opening `"` is a case marker. */
  EXPECT(run("\"'A\"", "\"A\"") == 0, 4);
  return 0;
}

int test_input_string_digraphs_translate(void) {
  /* `??/` must deliver `\` inside strings for escape sequences and
   * interpolation. */
  EXPECT(run("\"A?\?/NB\"", "\"a\\nb\"") == 0, 1);
  EXPECT(run("\"X = ?\?/(N)\"", "\"x = \\(n)\"") == 0, 2);
  /* Brace digraphs work inside strings too. */
  EXPECT(run("\"<%HI%>\"", "\"{hi}\"") == 0, 3);
  return 0;
}

int test_input_string_escape_passthrough(void) {
  /* A literal `\` followed by a char passes both bytes through (the
   * lexer sees the escape sequence as-is). On the //+ the user would
   * type `??/` to get `\`; on the host they can type `\` directly. */
  EXPECT(run("\"A\\NB\"", "\"a\\nb\"") == 0, 1);
  EXPECT(run("\"\\\\\"", "\"\\\\\"") == 0, 2);
  return 0;
}

int test_input_comments(void) {
  /* Line comment: auto-lowercase applies; case markers and digraphs
   * are literal so a comment can document them. */
  EXPECT(run("// 'HELLO <%", "// 'hello <%") == 0, 1);
  /* Block comment: same rules. */
  EXPECT(run("/* 'A ?\?/ */", "/* 'a ?\?/ */") == 0, 2);
  /* Comment ends, normal rules resume. */
  EXPECT(run("/* HI */ LET X", "/* hi */ let x") == 0, 3);
  EXPECT(run("// HI\nLET X", "// hi\nlet x") == 0, 4);
  return 0;
}

int test_input_empty_and_partial(void) {
  /* Empty input. */
  EXPECT(run("", "") == 0, 1);
  /* Unresolved trailing case marker is silently dropped. */
  EXPECT(run("X'", "x") == 0, 2);
  /* `?` and `??` without a third byte do not match `??/`. */
  EXPECT(run("?", "?") == 0, 3);
  EXPECT(run("??", "??") == 0, 4);
  /* `<` alone passes through. */
  EXPECT(run("<", "<") == 0, 5);
  return 0;
}

int test_input_idempotent(void) {
  /* Canonical bytes don't trip any translation rule. */
  EXPECT(run("let x = 5", "let x = 5") == 0, 1);
  EXPECT(run("var xs = [1, 2, 3]", "var xs = [1, 2, 3]") == 0, 2);
  EXPECT(run("if x > 0 { print(\"hi\") }",
             "if x > 0 { print(\"hi\") }") == 0, 3);
  return 0;
}
