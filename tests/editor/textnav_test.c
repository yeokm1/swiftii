/* Text navigation unit tests (host).
 *
 * Fixture buffer "ab\ncde\n\nf" — four logical lines, including an empty
 * one, so line-boundary and up/down column-clamp behaviour is covered:
 *
 *   idx: 0 1 2  3 4 5 6  7  8
 *   ch:  a b \n c d e \n \n f
 *   line 0 "ab"  (start 0, end 2)
 *   line 1 "cde" (start 3, end 6)
 *   line 2 ""    (start 7, end 7)
 *   line 3 "f"   (start 8, end 9 = len)
 */
#include <stdint.h>

#include "editor/gapbuf.h"
#include "editor/textnav.h"

#define EXPECT(cond, code) do { if (!(cond)) return (code); } while (0)

static void load_fixture(GapBuf *gb, uint8_t *store, uint16_t cap) {
  const char *s = "ab\ncde\n\nf";
  uint16_t i;
  gapbuf_init(gb, store, cap);
  for (i = 0; s[i]; ++i) gapbuf_insert(gb, (uint8_t)s[i]);
}

int test_textnav_line_start_end(void) {
  uint8_t store[32];
  GapBuf gb;
  load_fixture(&gb, store, (uint16_t)sizeof store);
  EXPECT(textnav_line_start(&gb, 5) == 3, 1);
  EXPECT(textnav_line_end(&gb, 5) == 6, 2);
  EXPECT(textnav_line_start(&gb, 0) == 0, 3);
  EXPECT(textnav_line_end(&gb, 0) == 2, 4);
  EXPECT(textnav_line_start(&gb, 7) == 7, 5); /* empty line */
  EXPECT(textnav_line_end(&gb, 7) == 7, 6);
  EXPECT(textnav_line_end(&gb, 8) == 9, 7);   /* last, unterminated -> len */
  return 0;
}

int test_textnav_col_and_index(void) {
  uint8_t store[32];
  GapBuf gb;
  load_fixture(&gb, store, (uint16_t)sizeof store);
  EXPECT(textnav_col(&gb, 5) == 2, 1);
  EXPECT(textnav_col(&gb, 1) == 1, 2);
  EXPECT(textnav_col(&gb, 7) == 0, 3);
  EXPECT(textnav_line_index(&gb, 5) == 1, 4);
  EXPECT(textnav_line_index(&gb, 0) == 0, 5);
  EXPECT(textnav_line_index(&gb, 7) == 2, 6);
  EXPECT(textnav_line_index(&gb, 8) == 3, 7);
  return 0;
}

int test_textnav_line_count_and_at(void) {
  uint8_t store[32];
  GapBuf gb;
  load_fixture(&gb, store, (uint16_t)sizeof store);
  EXPECT(textnav_line_count(&gb) == 4, 1);
  EXPECT(textnav_line_at(&gb, 0) == 0, 2);
  EXPECT(textnav_line_at(&gb, 1) == 3, 3);
  EXPECT(textnav_line_at(&gb, 2) == 7, 4);
  EXPECT(textnav_line_at(&gb, 3) == 8, 5);
  EXPECT(textnav_line_at(&gb, 99) == 8, 6); /* past end -> last line start */
  return 0;
}

int test_textnav_up_down_keeps_column(void) {
  uint8_t store[32];
  GapBuf gb;
  load_fixture(&gb, store, (uint16_t)sizeof store);
  /* From col 2 of "cde" (pos 5): up clamps onto "ab" (len 2) -> pos 2. */
  EXPECT(textnav_up(&gb, 5) == 2, 1);
  /* down from pos 5 lands on the empty line (col clamps to 0) -> pos 7. */
  EXPECT(textnav_down(&gb, 5) == 7, 2);
  /* Straight down/up at col 0 round-trips line0<->line1. */
  EXPECT(textnav_down(&gb, 0) == 3, 3);
  EXPECT(textnav_up(&gb, 3) == 0, 4);
  return 0;
}

int test_textnav_up_down_edges(void) {
  uint8_t store[32];
  GapBuf gb;
  load_fixture(&gb, store, (uint16_t)sizeof store);
  EXPECT(textnav_up(&gb, 1) == 1, 1);   /* first line -> unchanged */
  EXPECT(textnav_down(&gb, 8) == 8, 2); /* last line -> unchanged */
  return 0;
}

int test_textnav_empty_buffer(void) {
  uint8_t store[8];
  GapBuf gb;
  gapbuf_init(&gb, store, (uint16_t)sizeof store);
  EXPECT(textnav_line_count(&gb) == 1, 1);
  EXPECT(textnav_line_start(&gb, 0) == 0, 2);
  EXPECT(textnav_line_end(&gb, 0) == 0, 3);
  EXPECT(textnav_line_at(&gb, 0) == 0, 4);
  EXPECT(textnav_up(&gb, 0) == 0, 5);
  EXPECT(textnav_down(&gb, 0) == 0, 6);
  EXPECT(textnav_col(&gb, 0) == 0, 7);
  return 0;
}
