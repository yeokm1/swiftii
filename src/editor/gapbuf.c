/* Gap buffer implementation — see gapbuf.h for the model and invariants.
 *
 * Cursor moves copy bytes across the gap with explicit, direction-correct
 * loops rather than memmove: the source and destination ranges overlap
 * whenever the gap is smaller than the move distance, and choosing the
 * copy direction by hand keeps the overlap correct without pulling in
 * cc65's string.h. The loops are byte-at-a-time, which is fine at
 * human-rate editing (a full-buffer jump is rare and still well under a
 * frame on a 1 MHz 6502). */
#include "gapbuf.h"

uint16_t gapbuf_len(const GapBuf *gb) {
  return (uint16_t)(gb->gap_start + (gb->cap - gb->gap_end));
}

uint16_t gapbuf_pos(const GapBuf *gb) {
  return gb->gap_start;
}

void gapbuf_init(GapBuf *gb, uint8_t *storage, uint16_t cap) {
  gb->buf = storage;
  gb->cap = cap;
  gb->gap_start = 0;
  gb->gap_end = cap;
}

int gapbuf_insert(GapBuf *gb, uint8_t ch) {
  if (gb->gap_start == gb->gap_end) return 0; /* gap exhausted */
  gb->buf[gb->gap_start] = ch;
  ++gb->gap_start;
  return 1;
}

int gapbuf_delete_left(GapBuf *gb) {
  if (gb->gap_start == 0) return 0;
  /* The byte left of the cursor is simply absorbed into the gap. */
  --gb->gap_start;
  return 1;
}

int gapbuf_delete_right(GapBuf *gb) {
  if (gb->gap_end == gb->cap) return 0;
  /* The byte right of the cursor is absorbed into the gap. */
  ++gb->gap_end;
  return 1;
}

void gapbuf_move_left(GapBuf *gb, uint16_t n) {
  uint16_t k;
  uint16_t i;
  k = n > gb->gap_start ? gb->gap_start : n;
  /* Copy the k bytes ending at gap_start down to the bytes ending at
   * gap_end. Destination is above the source, so walk from the top to
   * stay overlap-safe. */
  for (i = 0; i < k; ++i) {
    gb->buf[gb->gap_end - 1 - i] = gb->buf[gb->gap_start - 1 - i];
  }
  gb->gap_start = (uint16_t)(gb->gap_start - k);
  gb->gap_end = (uint16_t)(gb->gap_end - k);
}

void gapbuf_move_right(GapBuf *gb, uint16_t n) {
  uint16_t avail;
  uint16_t k;
  uint16_t i;
  avail = (uint16_t)(gb->cap - gb->gap_end);
  k = n > avail ? avail : n;
  /* Copy the k bytes starting at gap_end down to gap_start. Destination
   * is below the source, so walk from the bottom. */
  for (i = 0; i < k; ++i) {
    gb->buf[gb->gap_start + i] = gb->buf[gb->gap_end + i];
  }
  gb->gap_start = (uint16_t)(gb->gap_start + k);
  gb->gap_end = (uint16_t)(gb->gap_end + k);
}

void gapbuf_move_to(GapBuf *gb, uint16_t pos) {
  uint16_t len;
  len = gapbuf_len(gb);
  if (pos > len) pos = len;
  if (pos < gb->gap_start) {
    gapbuf_move_left(gb, (uint16_t)(gb->gap_start - pos));
  } else if (pos > gb->gap_start) {
    gapbuf_move_right(gb, (uint16_t)(pos - gb->gap_start));
  }
}

uint8_t gapbuf_at(const GapBuf *gb, uint16_t pos) {
  /* Length inlined (not gapbuf_len()) — gapbuf_at is called per byte in the
   * editor's hot paths, and the extra call per byte was measurable on the
   * 1 MHz target. */
  if (pos >= (uint16_t)(gb->gap_start + (gb->cap - gb->gap_end))) return 0;
  if (pos < gb->gap_start) return gb->buf[pos];
  return gb->buf[gb->gap_end + (pos - gb->gap_start)];
}

uint16_t gapbuf_serialize(const GapBuf *gb, uint8_t *out, uint16_t cap) {
  uint16_t n;
  uint16_t i;
  uint16_t pre;
  uint16_t post;
  n = 0;
  pre = gb->gap_start;
  post = (uint16_t)(gb->cap - gb->gap_end);
  for (i = 0; i < pre && n < cap; ++i) out[n++] = gb->buf[i];
  for (i = 0; i < post && n < cap; ++i) out[n++] = gb->buf[gb->gap_end + i];
  return n;
}

int gapbuf_load(GapBuf *gb, const uint8_t *src, uint16_t len) {
  uint16_t i;
  if (len > gb->cap) return 0;
  /* Place the text at the top of the buffer so the cursor lands at 0:
   * everything becomes post-gap text and the gap is the leading slack. */
  gb->gap_start = 0;
  gb->gap_end = (uint16_t)(gb->cap - len);
  for (i = 0; i < len; ++i) gb->buf[gb->gap_end + i] = src[i];
  return 1;
}
