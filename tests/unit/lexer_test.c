/* Lexer unit tests.
 *
 * Each test feeds a short source string into the lexer and walks through
 * the token stream, asserting (token type, value) at each step. Host-only
 * with sanitizers — out-of-bounds reads on token text spans are caught
 * by ASAN.
 */

#include <stdint.h>
#include <string.h>

#include "lexer/lexer.h"

#define EXPECT(cond, code) do { if (!(cond)) return (code); } while (0)

static int next_is(Lexer *L, tok_t t) {
  lexer_next(L);
  return L->tok == t ? 1 : 0;
}

int test_lex_keywords_and_idents(void) {
  const char *src =
    "let var func return if else while for in true false nil hello";
  Lexer L;
  lexer_init(&L, src, (uint16_t)strlen(src));

  EXPECT(next_is(&L, TOK_LET),    1);
  EXPECT(next_is(&L, TOK_VAR),    2);
  EXPECT(next_is(&L, TOK_FUNC),   3);
  EXPECT(next_is(&L, TOK_RETURN), 4);
  EXPECT(next_is(&L, TOK_IF),     5);
  EXPECT(next_is(&L, TOK_ELSE),   6);
  EXPECT(next_is(&L, TOK_WHILE),  7);
  EXPECT(next_is(&L, TOK_FOR),    8);
  EXPECT(next_is(&L, TOK_IN),     9);
  EXPECT(next_is(&L, TOK_TRUE),   10);
  EXPECT(next_is(&L, TOK_FALSE),  11);
  EXPECT(next_is(&L, TOK_NIL),    12);
  EXPECT(next_is(&L, TOK_IDENT),  13);
  EXPECT(L.tok_len == 5, 14);
  EXPECT(memcmp(src + L.tok_pos, "hello", 5) == 0, 15);
  EXPECT(next_is(&L, TOK_EOF),    16);
  return 0;
}

int test_lex_integers(void) {
  /* Budget sweep (2026-05-23) dropped hex/binary/octal prefix
   * forms and underscore digit separators — decimal only now.
   *
   * Sub-slice (2026-05-24) widened the literal range from
   * 0..32767 to 0..65535 so Apple II addresses fit naturally:
   * `49200` ($C030, speaker click) and `49250` ($C062, paddle 0)
   * are now valid literals, stored as their two's-complement i16
   * bit patterns. */
  const char *src = "0 42 255 32767 32768 49200 65535";
  Lexer L;
  lexer_init(&L, src, (uint16_t)strlen(src));

  EXPECT(next_is(&L, TOK_INT), 1);
  EXPECT(L.int_val == 0, 2);
  EXPECT(next_is(&L, TOK_INT), 3);
  EXPECT(L.int_val == 42, 4);
  EXPECT(next_is(&L, TOK_INT), 5);
  EXPECT(L.int_val == 255, 6);
  EXPECT(next_is(&L, TOK_INT), 7);
  EXPECT(L.int_val == 32767, 8);
  /* Values above i16 positive max wrap to their two's-complement
   * representation. 32768 → -32768; 49200 → -16336; 65535 → -1. */
  EXPECT(next_is(&L, TOK_INT), 9);
  EXPECT(L.int_val == (int16_t)-32768, 10);
  EXPECT(next_is(&L, TOK_INT), 11);
  EXPECT(L.int_val == (int16_t)-16336, 12);
  EXPECT(next_is(&L, TOK_INT), 13);
  EXPECT(L.int_val == (int16_t)-1, 14);
  EXPECT(next_is(&L, TOK_EOF), 15);
  return 0;
}

int test_lex_int_overflow(void) {
  /* 65536 is the first value over u16 max; lexer must reject. */
  const char *src = "65536";
  Lexer L;
  lexer_init(&L, src, (uint16_t)strlen(src));
  lexer_next(&L);
  EXPECT(L.tok == TOK_ERROR, 1);
  EXPECT(L.err != (const char *)0, 2);
  return 0;
}

int test_lex_long_ident_accepted(void) {
  /* The lexer does NOT cap identifier length — long names must still tokenise
   * so builtin calls (e.g. listDirectory) and references resolve. The
   * 11-char symbol-table limit is enforced at the declaration site, not here. */
  const char *src = "thisIsAVeryLongIdentifier";  /* 25 chars */
  Lexer L;
  lexer_init(&L, src, (uint16_t)strlen(src));
  lexer_next(&L);
  EXPECT(L.tok == TOK_IDENT, 1);
  EXPECT(L.tok_len == 25, 2);
  return 0;
}

int test_lex_operators(void) {
  const char *src = "+ - * / % == != < <= > >= = ( ) { } [ ] , ; : ?? ->";
  Lexer L;
  lexer_init(&L, src, (uint16_t)strlen(src));

  EXPECT(next_is(&L, TOK_PLUS),     1);
  EXPECT(next_is(&L, TOK_MINUS),    2);
  EXPECT(next_is(&L, TOK_STAR),     3);
  EXPECT(next_is(&L, TOK_SLASH),    4);
  EXPECT(next_is(&L, TOK_PERCENT),  5);
  EXPECT(next_is(&L, TOK_EQ),       6);
  EXPECT(next_is(&L, TOK_NEQ),      7);
  EXPECT(next_is(&L, TOK_LT),       8);
  EXPECT(next_is(&L, TOK_LE),       9);
  EXPECT(next_is(&L, TOK_GT),       10);
  EXPECT(next_is(&L, TOK_GE),       11);
  EXPECT(next_is(&L, TOK_ASSIGN),   12);
  EXPECT(next_is(&L, TOK_LPAREN),   13);
  EXPECT(next_is(&L, TOK_RPAREN),   14);
  EXPECT(next_is(&L, TOK_LBRACE),   15);
  EXPECT(next_is(&L, TOK_RBRACE),   16);
  EXPECT(next_is(&L, TOK_LBRACKET), 17);
  EXPECT(next_is(&L, TOK_RBRACKET), 18);
  EXPECT(next_is(&L, TOK_COMMA),    19);
  EXPECT(next_is(&L, TOK_SEMI),     20);
  EXPECT(next_is(&L, TOK_COLON),    21);
  EXPECT(next_is(&L, TOK_QQ),       22);
  EXPECT(next_is(&L, TOK_ARROW),    23);
  EXPECT(next_is(&L, TOK_EOF),      24);
  return 0;
}

int test_lex_comments_and_newlines(void) {
  const char *src =
    "let x = 1 // trailing comment\n"
    "/* block\n comment */ var y = 2";
  Lexer L;
  lexer_init(&L, src, (uint16_t)strlen(src));

  EXPECT(next_is(&L, TOK_LET),     1);
  EXPECT(next_is(&L, TOK_IDENT),   2);
  EXPECT(next_is(&L, TOK_ASSIGN),  3);
  EXPECT(next_is(&L, TOK_INT),     4);
  EXPECT(L.int_val == 1, 5);
  EXPECT(next_is(&L, TOK_NEWLINE), 6);
  EXPECT(next_is(&L, TOK_VAR),     7);
  EXPECT(next_is(&L, TOK_IDENT),   8);
  EXPECT(next_is(&L, TOK_ASSIGN),  9);
  EXPECT(next_is(&L, TOK_INT),     10);
  EXPECT(L.int_val == 2, 11);
  EXPECT(next_is(&L, TOK_EOF),     12);
  return 0;
}

int test_lex_string_simple(void) {
  const char *src = "\"hello\" \"\"";
  Lexer L;
  lexer_init(&L, src, (uint16_t)strlen(src));
  lexer_next(&L);
  EXPECT(L.tok == TOK_STR, 1);
  EXPECT(L.tok_len == 5, 2);
  EXPECT(memcmp(src + L.tok_pos, "hello", 5) == 0, 3);
  lexer_next(&L);
  EXPECT(L.tok == TOK_STR, 4);
  EXPECT(L.tok_len == 0, 5);
  lexer_next(&L);
  EXPECT(L.tok == TOK_EOF, 6);
  return 0;
}

int test_lex_string_escapes(void) {
  /* The lexer accepts the escape; the compiler is what processes it.
   * tok_len spans the raw bytes between the quotes, escape included. */
  const char *src = "\"a\\nb\\tc\\\\d\"";
  Lexer L;
  lexer_init(&L, src, (uint16_t)strlen(src));
  lexer_next(&L);
  EXPECT(L.tok == TOK_STR, 1);
  /* Raw text "a\nb\tc\\d" — 10 source bytes between the quotes. */
  EXPECT(L.tok_len == 10, 2);
  return 0;
}

int test_lex_string_bad_escape(void) {
  const char *src = "\"\\q\"";
  Lexer L;
  lexer_init(&L, src, (uint16_t)strlen(src));
  lexer_next(&L);
  EXPECT(L.tok == TOK_ERROR, 1);
  return 0;
}

int test_lex_string_interp_bounds(void) {
  /* "x = \(a + (b * 2))" — interpolation with nested parens. The
   * lexer must include the whole \( ... ) span inside the string
   * token's bounds. */
  const char *src = "\"x = \\(a + (b * 2))\"";
  Lexer L;
  lexer_init(&L, src, (uint16_t)strlen(src));
  lexer_next(&L);
  EXPECT(L.tok == TOK_STR, 1);
  EXPECT(L.tok_len == 18, 2);     /* x_=_\(a_+_(b_*_2)) = 18 chars */
  return 0;
}

int test_lex_string_unterm_interp(void) {
  /* Interpolation that never closes. */
  const char *src = "\"\\(a + b\"";
  Lexer L;
  lexer_init(&L, src, (uint16_t)strlen(src));
  lexer_next(&L);
  EXPECT(L.tok == TOK_ERROR, 1);
  return 0;
}
