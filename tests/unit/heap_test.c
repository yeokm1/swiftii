/* Heap allocator unit tests.
 *
 * Verifies bump allocation, header layout, refcount, and the LIFO
 * topmost-release reclaim path. */

#include <stdint.h>
#include <string.h>

#include "common/config.h"
#include "runtime/heap.h"

#define EXPECT(cond, code) do { if (!(cond)) return (code); } while (0)

int test_heap_alloc_payload(void) {
  heap_off_t a;
  unsigned char *p;
  heap_reset();
  a = heap_alloc(5);
  EXPECT(a != HEAP_NULL, 1);
  EXPECT(a >= (heap_off_t)STRING_POOL_SLOTS, 2);
  EXPECT(heap_len(a) == 5, 3);
  p = heap_payload(a);
  EXPECT(p != (unsigned char *)0, 4);
  memcpy(p, "hello", 5);
  /* Re-read. */
  EXPECT(memcmp(heap_payload(a), "hello", 5) == 0, 5);
  return 0;
}

int test_heap_multiple_allocs_distinct(void) {
  heap_off_t a, b;
  heap_reset();
  a = heap_alloc(4);
  b = heap_alloc(4);
  EXPECT(a != HEAP_NULL && b != HEAP_NULL, 1);
  EXPECT(a != b, 2);
  /* Layout: header(4) + payload(4) = 8 bytes per block. */
  EXPECT((uint16_t)(b - a) == 8, 3);
  return 0;
}

int test_heap_topmost_release_reclaims(void) {
  heap_off_t a, b, c;
  uint16_t before;
  heap_reset();
  a = heap_alloc(4);
  before = heap_free_bytes();
  b = heap_alloc(4);
  EXPECT(heap_free_bytes() == (uint16_t)(before - 8), 1);
  heap_release(b);                 /* topmost: should rewind */
  EXPECT(heap_free_bytes() == before, 2);
  /* New alloc reuses the same slot. */
  c = heap_alloc(4);
  EXPECT(c == b, 3);
  (void)a;
  return 0;
}

int test_heap_mid_release_leaks(void) {
  heap_off_t a, b;
  uint16_t free_after_b;
  heap_reset();
  a = heap_alloc(4);
  b = heap_alloc(4);
  (void)b;
  free_after_b = heap_free_bytes();
  heap_release(a);                 /* not topmost: stays leaked */
  EXPECT(heap_free_bytes() == free_after_b, 1);
  return 0;
}

int test_heap_retain_release_balanced(void) {
  heap_off_t a;
  uint16_t before;
  heap_reset();
  a = heap_alloc(4);
  before = heap_free_bytes();
  heap_retain(a);                  /* rc=2 */
  heap_release(a);                 /* rc=1, no reclaim */
  EXPECT(heap_free_bytes() == before, 1);
  heap_release(a);                 /* rc=0, topmost: reclaim */
  EXPECT(heap_free_bytes() > before, 2);
  return 0;
}

int test_heap_oom_returns_null(void) {
  heap_off_t a;
  uint16_t huge;
  heap_reset();
  huge = (uint16_t)(HEAP_SIZE);    /* larger than any single block */
  a = heap_alloc(huge);
  EXPECT(a == HEAP_NULL, 1);
  return 0;
}

int test_heap_reset_clears(void) {
  heap_off_t a, b;
  heap_reset();
  a = heap_alloc(4);
  heap_reset();
  b = heap_alloc(4);
  EXPECT(a == b, 1);               /* fresh allocations resume at base */
  return 0;
}
