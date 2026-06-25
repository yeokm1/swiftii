/* (Doc 016) — streaming source window.
 *
 * Turns the Compiler's fixed source buffer into a sliding window over an
 * open file, so source size is bounded by disk, not RAM. The window
 * slides only at statement separators (`parser_skip_separators` calls the
 * Parser's refill hook while the current token is a dead newline/`;`),
 * which is safe because nothing in the compiler holds a source `(pos,len)`
 * across a separator: symbol tables copy names (IDENT_MAX), string
 * interpolation re-lexes only inside the current TOK_STR, and every
 * deferred `tok_pos` is consumed within its own statement (see doc 016
 * § Findings).
 *
 * Consequence: a single statement (including any string literal) must fit
 * the window. If the lexer drains the window mid-statement with file
 * bytes still unread, the compile fails at that point and `eof` stays 0 —
 * callers report "statement too long" off that flag.
 *
 * Family-B-only (WITH_SWB): linked into the Compiler binary + host tests,
 * never the Family A interpreters.
 */
#ifndef SWIFTII_SRCWIN_H
#define SWIFTII_SRCWIN_H

#ifdef WITH_SWB

#include <stdint.h>
#include "../runtime/prodos.h"

struct parser;

typedef struct srcwin {
  char *win;          /* caller-owned window buffer */
  uint16_t cap;       /* window capacity in bytes */
  pf_handle h;        /* open source file */
  unsigned char eof;  /* 1 once the file is fully read */
  uint16_t slides;    /* completed window slides (progress trigger) */
  uint16_t rd;        /* total bytes read from the file so far */
  uint16_t total;     /* file size (pf_size at open; 0 if unknown) —
                         rd/total drive compiler_main's percent display */
} SrcWin;

/* Open `path` and fill the window. Returns the initial byte count
 * (0..cap), or -1 if the file cannot be opened. On success the file
 * stays open for refills — call srcwin_close() after compiling (and
 * before any other pf_open_*: one MLI I/O buffer, one open at a time). */
long srcwin_open(SrcWin *w, char *win, uint16_t cap, const char *path);

/* Close the underlying file (idempotent). */
void srcwin_close(SrcWin *w);

/* Parser refill hook (install via compiler_set_refill with the SrcWin as
 * ctx). Called between statements while the current token is a dead
 * separator: once the scan point passes half the window, slides the tail
 * down (keeping the current token resident) and tops up from disk,
 * rebasing the lexer's pos/len/tok_pos. No-op after EOF. */
void srcwin_refill(struct parser *p);

#endif /* WITH_SWB */

#endif /* SWIFTII_SRCWIN_H */
