/* Gap buffer unit tests (host, clang + ASan/UBSan).
 *
 * Each test returns 0 on pass, nonzero on fail; the codes increase down
 * each function so a failing run points at the exact assertion. The gap
 * buffer is pure C with caller-provided storage, so these exercise it
 * directly with small stack buffers — no platform layer involved.
 */
#include <stdint.h>
#include <string.h>

#include "editor/gapbuf.h"

#define EXPECT(cond, code) do { if (!(cond)) return (code); } while (0)

/* Serialize the buffer and compare against a NUL-terminated expected
 * string. Returns 1 on match. */
static int serial_eq(const GapBuf *gb, const char *expect) {
  uint8_t out[64];
  uint16_t n;
  uint16_t want;
  n = gapbuf_serialize(gb, out, (uint16_t)sizeof out);
  want = (uint16_t)strlen(expect);
  if (n != want) return 0;
  return memcmp(out, expect, n) == 0;
}

static void put(GapBuf *gb, const char *s) {
  while (*s) gapbuf_insert(gb, (uint8_t)*s++);
}

int test_gapbuf_init_empty(void) {
  uint8_t store[16];
  GapBuf gb;
  gapbuf_init(&gb, store, (uint16_t)sizeof store);
  EXPECT(gapbuf_len(&gb) == 0, 1);
  EXPECT(gapbuf_pos(&gb) == 0, 2);
  EXPECT(serial_eq(&gb, ""), 3);
  return 0;
}

int test_gapbuf_insert_basic(void) {
  uint8_t store[16];
  GapBuf gb;
  gapbuf_init(&gb, store, (uint16_t)sizeof store);
  put(&gb, "hi");
  EXPECT(gapbuf_len(&gb) == 2, 1);
  EXPECT(gapbuf_pos(&gb) == 2, 2);
  EXPECT(gapbuf_at(&gb, 0) == 'h', 3);
  EXPECT(gapbuf_at(&gb, 1) == 'i', 4);
  EXPECT(serial_eq(&gb, "hi"), 5);
  return 0;
}

int test_gapbuf_insert_full(void) {
  uint8_t store[3];
  GapBuf gb;
  gapbuf_init(&gb, store, (uint16_t)sizeof store);
  EXPECT(gapbuf_insert(&gb, 'a') == 1, 1);
  EXPECT(gapbuf_insert(&gb, 'b') == 1, 2);
  EXPECT(gapbuf_insert(&gb, 'c') == 1, 3);
  EXPECT(gapbuf_insert(&gb, 'd') == 0, 4); /* full */
  EXPECT(gapbuf_len(&gb) == 3, 5);
  EXPECT(serial_eq(&gb, "abc"), 6);
  return 0;
}

int test_gapbuf_delete_left(void) {
  uint8_t store[16];
  GapBuf gb;
  gapbuf_init(&gb, store, (uint16_t)sizeof store);
  put(&gb, "abc");
  EXPECT(gapbuf_delete_left(&gb) == 1, 1);
  EXPECT(serial_eq(&gb, "ab"), 2);
  EXPECT(gapbuf_pos(&gb) == 2, 3);
  gapbuf_move_to(&gb, 0);
  EXPECT(gapbuf_delete_left(&gb) == 0, 4); /* at start */
  EXPECT(serial_eq(&gb, "ab"), 5);
  return 0;
}

int test_gapbuf_delete_right(void) {
  uint8_t store[16];
  GapBuf gb;
  gapbuf_init(&gb, store, (uint16_t)sizeof store);
  put(&gb, "abc");
  gapbuf_move_to(&gb, 0);
  EXPECT(gapbuf_delete_right(&gb) == 1, 1);
  EXPECT(serial_eq(&gb, "bc"), 2);
  EXPECT(gapbuf_pos(&gb) == 0, 3);
  gapbuf_move_to(&gb, gapbuf_len(&gb));
  EXPECT(gapbuf_delete_right(&gb) == 0, 4); /* at end */
  EXPECT(serial_eq(&gb, "bc"), 5);
  return 0;
}

int test_gapbuf_insert_middle(void) {
  uint8_t store[16];
  GapBuf gb;
  gapbuf_init(&gb, store, (uint16_t)sizeof store);
  put(&gb, "ac");
  gapbuf_move_left(&gb, 1);
  EXPECT(gapbuf_pos(&gb) == 1, 1);
  gapbuf_insert(&gb, 'b');
  EXPECT(serial_eq(&gb, "abc"), 2);
  EXPECT(gapbuf_pos(&gb) == 2, 3);
  return 0;
}

int test_gapbuf_move_clamps(void) {
  uint8_t store[16];
  GapBuf gb;
  gapbuf_init(&gb, store, (uint16_t)sizeof store);
  put(&gb, "ab");
  gapbuf_move_left(&gb, 10);
  EXPECT(gapbuf_pos(&gb) == 0, 1);
  gapbuf_move_right(&gb, 10);
  EXPECT(gapbuf_pos(&gb) == 2, 2);
  gapbuf_move_to(&gb, 100);
  EXPECT(gapbuf_pos(&gb) == 2, 3);
  EXPECT(serial_eq(&gb, "ab"), 4); /* moves never corrupt the text */
  return 0;
}

int test_gapbuf_serialize_bounds(void) {
  uint8_t store[16];
  uint8_t out[3];
  GapBuf gb;
  uint16_t n;
  gapbuf_init(&gb, store, (uint16_t)sizeof store);
  put(&gb, "hello");
  n = gapbuf_serialize(&gb, out, (uint16_t)sizeof out);
  EXPECT(n == 3, 1);
  EXPECT(memcmp(out, "hel", 3) == 0, 2);
  return 0;
}

int test_gapbuf_load(void) {
  uint8_t store[16];
  GapBuf gb;
  gapbuf_init(&gb, store, (uint16_t)sizeof store);
  put(&gb, "junk");
  EXPECT(gapbuf_load(&gb, (const uint8_t *)"world", 5) == 1, 1);
  EXPECT(gapbuf_len(&gb) == 5, 2);
  EXPECT(gapbuf_pos(&gb) == 0, 3);
  EXPECT(serial_eq(&gb, "world"), 4);
  /* Editing after a load works across the relocated gap. */
  gapbuf_move_to(&gb, 5);
  put(&gb, "!");
  EXPECT(serial_eq(&gb, "world!"), 5);
  return 0;
}

int test_gapbuf_load_too_big(void) {
  uint8_t store[4];
  GapBuf gb;
  gapbuf_init(&gb, store, (uint16_t)sizeof store);
  EXPECT(gapbuf_load(&gb, (const uint8_t *)"hello", 5) == 0, 1);
  /* On rejection the buffer is left empty (init state), not corrupted. */
  EXPECT(gapbuf_load(&gb, (const uint8_t *)"hi", 2) == 1, 2);
  EXPECT(serial_eq(&gb, "hi"), 3);
  return 0;
}

int test_gapbuf_at_out_of_range(void) {
  uint8_t store[16];
  GapBuf gb;
  gapbuf_init(&gb, store, (uint16_t)sizeof store);
  put(&gb, "ab");
  EXPECT(gapbuf_at(&gb, 0) == 'a', 1);
  EXPECT(gapbuf_at(&gb, 1) == 'b', 2);
  EXPECT(gapbuf_at(&gb, 2) == 0, 3); /* == len */
  EXPECT(gapbuf_at(&gb, 99) == 0, 4);
  return 0;
}

int test_gapbuf_round_trip(void) {
  uint8_t store[32];
  GapBuf gb;
  gapbuf_init(&gb, store, (uint16_t)sizeof store);
  /* Build "let x = 1", then fix it to "let y = 2" via edits. */
  put(&gb, "let x = 1");
  gapbuf_move_to(&gb, 4);          /* between "let " and "x" */
  gapbuf_delete_right(&gb);        /* drop 'x' */
  gapbuf_insert(&gb, 'y');         /* -> "let y = 1" */
  gapbuf_move_to(&gb, gapbuf_len(&gb));
  gapbuf_delete_left(&gb);         /* drop '1' */
  gapbuf_insert(&gb, '2');         /* -> "let y = 2" */
  EXPECT(serial_eq(&gb, "let y = 2"), 1);
  EXPECT(gapbuf_len(&gb) == 9, 2);
  return 0;
}
