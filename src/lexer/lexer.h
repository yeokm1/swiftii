/* Streaming lexer interface.
 *
 * Implementation. The lexer reads from a single in-RAM buffer
 * (the REPL line, or a file's contents loaded by `source_stream`). The
 * abstraction over disk vs. buffer lives in `file_runner/source_stream.c`
 * — by the time bytes reach the lexer, they are always in a buffer.
 *
 * Usage:
 *   Lexer L;
 *   lexer_init(&L, src, len);
 *   for (lexer_next(&L); L.tok != TOK_EOF; lexer_next(&L)) {
 *     ... inspect L.tok, L.tok_pos, L.tok_len, L.int_val ...
 *   }
 *
 * The lexer does not allocate. Token text is identified by (pos, len) into
 * the original source buffer. Callers must not free the buffer while the
 * lexer is in use.
 */
#ifndef SWIFTII_LEXER_H
#define SWIFTII_LEXER_H

#include <stdint.h>
#include "tokens.h"

typedef struct lexer {
  const char *src;       /* source buffer; not modified */
  uint16_t pos;          /* next byte to scan */
  uint16_t len;          /* bytes in buffer */
  uint16_t line;         /* 1-based current line */

  /* Last token state — written by lexer_next(). */
  tok_t    tok;
  uint16_t tok_pos;      /* offset of first char of last token */
  uint16_t tok_len;      /* length of last token, in source bytes */
  uint16_t tok_line;     /* line at start of last token */
  int16_t  int_val;      /* parsed value when tok == TOK_INT */

  /* On TOK_ERROR, a short static message. NULL otherwise. */
  const char *err;
} Lexer;

/* Bind the lexer to a source buffer. The buffer must remain valid
 * for the lifetime of L; the lexer does not copy or own it. Resets
 * the token state and line counter; the first token is read by the
 * first call to `lexer_next`. */
void lexer_init(Lexer *L, const char *src, uint16_t len);

/* Advance to the next token. Updates L->tok and the tok_* fields. On
 * EOF the lexer becomes sticky: repeated calls keep returning TOK_EOF. */
void lexer_next(Lexer *L);

#endif /* SWIFTII_LEXER_H */
