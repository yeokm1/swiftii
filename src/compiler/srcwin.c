/* (Doc 016) — streaming source window. See srcwin.h. */
#include "srcwin.h"

#ifdef WITH_SWB

#include <string.h>

#include "parser.h"

/* Top up window bytes [L_len..cap) from the file; returns the new length.
 * Sets w->eof when the file is exhausted. */
static uint16_t top_up(SrcWin *w, uint16_t len) {
  int n;
  while (!w->eof && len < w->cap) {
    n = pf_read(w->h, (unsigned char *)w->win + len,
                (uint16_t)(w->cap - len));
    if (n <= 0) { w->eof = 1; break; }
    len = (uint16_t)(len + n);
    w->rd = (uint16_t)(w->rd + n);
  }
  return len;
}

long srcwin_open(SrcWin *w, char *win, uint16_t cap, const char *path) {
  w->win = win;
  w->cap = cap;
  w->eof = 0;
  w->slides = 0;
  w->rd = 0;
  w->h = pf_open_read(path);
  if (w->h == PF_BAD) { w->eof = 1; w->total = 0; return -1; }
  w->total = pf_size(w->h);
  return (long)top_up(w, 0);
}

void srcwin_close(SrcWin *w) {
  if (w->h != PF_BAD) {
    pf_close(w->h);
    w->h = PF_BAD;
  }
}

void srcwin_refill(struct parser *p) {
  SrcWin *w = (SrcWin *)p->refill_ctx;
  Lexer *L = &p->L;
  uint16_t delta;

  if (w->eof) return;
  /* Only slide once the scan point is past half the window — refills then
   * cost one ~cap/2 memmove + read per ~cap/2 of source. */
  if (L->pos < (uint16_t)(w->cap / 2)) return;

  /* Keep the current (separator) token resident so every live lexer
   * offset stays in-window; everything before it is dead. */
  delta = L->tok_pos;
  if (delta == 0) return;

  memmove(w->win, w->win + delta, (size_t)(L->len - delta));
  L->len = (uint16_t)(L->len - delta);
  L->pos = (uint16_t)(L->pos - delta);
  L->tok_pos = 0;

  L->len = top_up(w, L->len);
  ++w->slides;
}

#endif /* WITH_SWB */
