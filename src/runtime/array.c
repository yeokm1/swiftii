/* Array runtime — see array.h and docs/contributing/design/007-arrays.md. */
#include "array.h"

#include <stdint.h>
#include "value.h"

/* Header offsets within the payload (after heap's 4-byte block header). */
#define COUNT_OFF    0
#define CAP_OFF      2
#define SLOTS_OFF    4
#define SLOT_BYTES   3   /* one tagged Value = 1 tag + 2 payload */

static uint16_t pl_read_u16(const unsigned char *pl, uint16_t off) {
  return (uint16_t)pl[off] | ((uint16_t)pl[off + 1] << 8);
}

static void pl_write_u16(unsigned char *pl, uint16_t off, uint16_t v) {
  pl[off]     = (unsigned char)(v & 0xFF);
  pl[off + 1] = (unsigned char)((v >> 8) & 0xFF);
}

/* cc65 disallows returning structs ≥ 3 bytes by value (the entire
 * Value is exactly 3 bytes). Use an out-pointer instead. */
static void read_slot(const unsigned char *pl, uint16_t i, Value *out) {
  uint16_t off = (uint16_t)(SLOTS_OFF + i * SLOT_BYTES);
  out->tag = pl[off];
  out->lo  = pl[off + 1];
  out->hi  = pl[off + 2];
}

static void write_slot(unsigned char *pl, uint16_t i, const Value *v) {
  uint16_t off = (uint16_t)(SLOTS_OFF + i * SLOT_BYTES);
  pl[off]     = v->tag;
  pl[off + 1] = v->lo;
  pl[off + 2] = v->hi;
}

heap_off_t array_new(uint16_t initial_capacity) {
  uint16_t cap;
  uint16_t bytes;
  heap_off_t off;
  unsigned char *pl;
  uint16_t i;

  cap = initial_capacity == 0 ? (uint16_t)ARRAY_INIT_CAPACITY : initial_capacity;
  bytes = (uint16_t)(SLOTS_OFF + (uint16_t)cap * (uint16_t)SLOT_BYTES);
  off = heap_alloc(bytes);
  if (off == HEAP_NULL) return HEAP_NULL;
  pl = heap_payload(off);
  pl_write_u16(pl, COUNT_OFF, 0);
  pl_write_u16(pl, CAP_OFF, cap);
  /* Zero the slots so a stray read sees T_NIL rather than garbage. */
  for (i = SLOTS_OFF; i < bytes; ++i) pl[i] = 0;
  return off;
}

uint16_t array_count(heap_off_t arr) {
  unsigned char *pl;
  pl = heap_payload(arr);
  if (!pl) return 0;
  return pl_read_u16(pl, COUNT_OFF);
}

uint16_t array_capacity(heap_off_t arr) {
  unsigned char *pl;
  pl = heap_payload(arr);
  if (!pl) return 0;
  return pl_read_u16(pl, CAP_OFF);
}

swiftii_err_t array_get(heap_off_t arr, uint16_t i, Value *out) {
  unsigned char *pl;
  uint16_t count;
  pl = heap_payload(arr);
  if (!pl) return SE_RUNTIME;
  count = pl_read_u16(pl, COUNT_OFF);
  if (i >= count) return SE_RUNTIME;
  read_slot(pl, i, out);
  return SE_OK;
}

swiftii_err_t array_set(heap_off_t arr, uint16_t i, const Value *v) {
  unsigned char *pl;
  uint16_t count;
  Value old;
  pl = heap_payload(arr);
  if (!pl) return SE_RUNTIME;
  count = pl_read_u16(pl, COUNT_OFF);
  if (i >= count) return SE_RUNTIME;
  read_slot(pl, i, &old);
  write_slot(pl, i, v);
  value_retain(v);
  value_release(&old);
  return SE_OK;
}

swiftii_err_t array_append(heap_off_t *arr_inout, const Value *v) {
  heap_off_t arr;
  unsigned char *pl;
  uint16_t count;
  uint16_t cap;

  arr = *arr_inout;
  pl = heap_payload(arr);
  if (!pl) return SE_RUNTIME;
  count = pl_read_u16(pl, COUNT_OFF);
  cap   = pl_read_u16(pl, CAP_OFF);

  if (count >= cap) {
    /* Doubling growth (per design doc 007). The old block stays in
     * place as dead bytes unless it was topmost; demos
     * the LIFO reclaim catches the common case. */
    uint16_t new_cap;
    heap_off_t new_arr;
    unsigned char *new_pl;
    uint16_t i;
    uint16_t bytes_to_copy;

    new_cap = (uint16_t)(cap * 2);
    if (new_cap < (uint16_t)ARRAY_INIT_CAPACITY) new_cap = (uint16_t)ARRAY_INIT_CAPACITY;
    new_arr = array_new(new_cap);
    if (new_arr == HEAP_NULL) return SE_OOM;
    /* heap_payload pointers can be re-fetched after heap_alloc — the
     * underlying buffer is fixed BSS, but the bump pointer may have
     * advanced past dead bytes. Re-read both pointers post-alloc. */
    pl     = heap_payload(arr);
    new_pl = heap_payload(new_arr);
    pl_write_u16(new_pl, COUNT_OFF, count);
    bytes_to_copy = (uint16_t)(count * (uint16_t)SLOT_BYTES);
    for (i = 0; i < bytes_to_copy; ++i) {
      new_pl[SLOTS_OFF + i] = pl[SLOTS_OFF + i];
    }
    /* Drop the old array's heap reference. Elements transferred to
     * new_arr keep their refcounts (we didn't retain/release here). */
    heap_release(arr);
    arr = new_arr;
    pl  = new_pl;
    *arr_inout = arr;
  }

  /* Now append v at slot `count`, then bump count. */
  write_slot(pl, count, v);
  value_retain(v);
  pl_write_u16(pl, COUNT_OFF, (uint16_t)(count + 1));
  return SE_OK;
}

#if defined(WITH_SWIFTSAT) || defined(WITH_SWIFTAUX) || defined(WITH_SWB) || \
    !defined(__CC65__)
void array_truncate(heap_off_t arr, uint16_t n) {
  unsigned char *pl;
  pl = heap_payload(arr);
  if (!pl) return;
  pl_write_u16(pl, COUNT_OFF, n);
}
#endif

void array_init_from_stack(heap_off_t arr, const Value *src, uint8_t n) {
  unsigned char *pl;
  uint8_t i;
  pl = heap_payload(arr);
  if (!pl) return;
  for (i = 0; i < n; ++i) {
    write_slot(pl, i, &src[i]);
  }
  pl_write_u16(pl, COUNT_OFF, n);
}

void array_release_elements(heap_off_t arr) {
  unsigned char *pl;
  uint16_t count;
  uint16_t i;
  Value v;

  pl = heap_payload(arr);
  if (!pl) return;
  count = pl_read_u16(pl, COUNT_OFF);
  for (i = 0; i < count; ++i) {
    read_slot(pl, i, &v);
    value_release(&v);
  }
  /* Don't zero count — heap_release will reclaim or mark dead. */
}
