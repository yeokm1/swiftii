/* Custom heap allocator with reference counting.
 *
 * Implementation. See heap.h for the API and
 * docs/contributing/design/002-heap-and-strings.md for the rationale.
 *
 * Layout: a single fixed-size BSS buffer. The bump pointer starts at
 * STRING_POOL_SLOTS (so heap offsets can be distinguished from pool
 * indices in a T_STR Value payload, see runtime/builtins.c). Every
 * block is preceded by a 4-byte header storing refcount and payload
 * length, both little-endian u16.
 *
 * Reclaim policy: when a block's refcount hits zero AND it is the
 * topmost allocation, the bump pointer rewinds past its header and
 * payload, so that expression-temporary strings (e.g. the result of
 * an a + b that gets immediately printed and popped) don't leak the
 * heap. Anything mid-heap that hits zero stays dead until the next
 * heap_reset.
 */
#include "heap.h"

#include <stdint.h>
#include "../common/config.h"

#define HEADER_SIZE 4

static unsigned char s_heap[HEAP_SIZE];
static heap_off_t    s_heap_ptr;

void heap_reset(void) {
  uint16_t i;
  for (i = 0; i < (uint16_t)HEAP_SIZE; ++i) s_heap[i] = 0;
  s_heap_ptr = (heap_off_t)STRING_POOL_SLOTS;
}

static uint16_t read_u16(heap_off_t off) {
  return (uint16_t)s_heap[off] | ((uint16_t)s_heap[off + 1] << 8);
}

static void write_u16(heap_off_t off, uint16_t v) {
  s_heap[off]     = (unsigned char)(v & 0xFF);
  s_heap[off + 1] = (unsigned char)((v >> 8) & 0xFF);
}

/* Bounds check: `off` must be a valid header position. */
static int valid_off(heap_off_t off) {
  if (off < (heap_off_t)STRING_POOL_SLOTS) return 0;
  if (off >= s_heap_ptr) return 0;
  if ((uint16_t)(off + HEADER_SIZE) > (uint16_t)HEAP_SIZE) return 0;
  return 1;
}

heap_off_t heap_alloc(uint16_t payload_len) {
  uint32_t needed;
  heap_off_t off;

  needed = (uint32_t)payload_len + (uint32_t)HEADER_SIZE;
  if (needed > (uint32_t)HEAP_SIZE) return HEAP_NULL;
  if ((uint32_t)s_heap_ptr + needed > (uint32_t)HEAP_SIZE) return HEAP_NULL;

  off = s_heap_ptr;
  write_u16(off, 1);                 /* refcount */
  write_u16(off + 2, payload_len);   /* payload_len */
  s_heap_ptr = (heap_off_t)(s_heap_ptr + needed);
  return off;
}

unsigned char *heap_payload(heap_off_t off) {
  if (!valid_off(off)) return (unsigned char *)0;
  return &s_heap[off + HEADER_SIZE];
}

uint16_t heap_len(heap_off_t off) {
  if (!valid_off(off)) return 0;
  return read_u16(off + 2);
}

uint16_t heap_refcount(heap_off_t off) {
  if (!valid_off(off)) return 0;
  return read_u16(off);
}

void heap_retain(heap_off_t off) {
  uint16_t rc;
  if (!valid_off(off)) return;
  rc = read_u16(off);
  if (rc == 0xFFFF) return;          /* saturated; don't wrap */
  write_u16(off, (uint16_t)(rc + 1));
}

void heap_release(heap_off_t off) {
  uint16_t rc;
  uint16_t plen;
  uint16_t total;

  if (!valid_off(off)) return;
  rc = read_u16(off);
  if (rc == 0) return;               /* already dead */
  --rc;
  write_u16(off, rc);
  if (rc != 0) return;

  /* Topmost block? Reclaim by rewinding the bump pointer. */
  plen = read_u16(off + 2);
  total = (uint16_t)(plen + HEADER_SIZE);
  if ((uint16_t)(off + total) == s_heap_ptr) {
    s_heap_ptr = off;
  }
}

uint16_t heap_free_bytes(void) {
  if (s_heap_ptr >= (heap_off_t)HEAP_SIZE) return 0;
  return (uint16_t)((uint16_t)HEAP_SIZE - s_heap_ptr);
}

heap_off_t heap_savepoint(void) {
  return s_heap_ptr;
}

void heap_rollback(heap_off_t saved) {
  if (saved < (heap_off_t)STRING_POOL_SLOTS) return;
  if (saved > s_heap_ptr) return;
  s_heap_ptr = saved;
}

#ifdef WITH_SWB
/* `.swb` constant-pool accessors — Family B (Compiler/Runner) only, gated
 * out of the Family A interpreters which sit at the MAIN ceiling (these
 * three functions cost ~242 B that SWIFTSAT's ~1 B headroom can't take).
 * Enabled for host builds and the cc65 compiler/runner targets. */
uint16_t heap_const_image(const unsigned char **out) {
  *out = &s_heap[STRING_POOL_SLOTS];
  if (s_heap_ptr <= (heap_off_t)STRING_POOL_SLOTS) return 0;
  return (uint16_t)(s_heap_ptr - (heap_off_t)STRING_POOL_SLOTS);
}

void heap_load_const(const unsigned char *src, uint16_t len) {
  uint16_t i;
  if ((uint32_t)STRING_POOL_SLOTS + (uint32_t)len > (uint32_t)HEAP_SIZE) return;
  for (i = 0; i < len; ++i) s_heap[STRING_POOL_SLOTS + i] = src[i];
  s_heap_ptr = (heap_off_t)((uint16_t)STRING_POOL_SLOTS + len);
}
#endif /* WITH_SWB */
