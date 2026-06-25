/* Keyword lookup.
 *
 * Sorted by length first, then alphabetically — `keyword_lookup` does a
 * linear scan within the equal-length run because the keyword set is
 * tiny (≤ 16). A binary search or perfect-hash buys nothing here and
 * costs RODATA.
 *
 * Identifier text may extend past the keyword length; the caller has
 * already isolated the identifier span by `len`. Any mismatch in length
 * is a quick reject.
 */
#include <stdint.h>
#include "tokens.h"

struct kw_entry {
  const char *text;
  uint8_t     len;
  tok_t       tok;
};

static const struct kw_entry k_keywords[] = {
  { "if",      2, TOK_IF },
  { "in",      2, TOK_IN },
  { "let",     3, TOK_LET },
  { "var",     3, TOK_VAR },
  { "for",     3, TOK_FOR },
  { "nil",     3, TOK_NIL },
  { "else",    4, TOK_ELSE },
  { "func",    4, TOK_FUNC },
  { "true",    4, TOK_TRUE },
  { "break",   5, TOK_BREAK },
  { "false",   5, TOK_FALSE },
  { "while",   5, TOK_WHILE },
  { "return",  6, TOK_RETURN },
};

#define KW_COUNT ((uint8_t)(sizeof(k_keywords) / sizeof(k_keywords[0])))

/* Returns the keyword token, or 0 (TOK_EOF) for "not a keyword".
 * Caller substitutes TOK_IDENT for a 0 return.
 *
 * Exact lowercase match. The //+ input layer (rev 3, design doc 003)
 * delivers canonical lowercase bytes to the lexer, so the inner-loop
 * case-fold from rev 2 is gone — keywords typed as `LET` arrive here
 * as `let`. */
tok_t keyword_lookup(const char *text, uint16_t len) {
  uint8_t i;
  uint16_t j;
  const char *kw;

  if (len < 2 || len > 6) return 0;

  for (i = 0; i < KW_COUNT; ++i) {
    if (k_keywords[i].len != len) continue;
    kw = k_keywords[i].text;
    for (j = 0; j < len; ++j) {
      if (kw[j] != text[j]) break;
    }
    if (j == len) return k_keywords[i].tok;
  }
  return 0;
}
