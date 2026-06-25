/* Static string pool for compile-time-known string constants.
 *
 * Has exactly one entry: "hello, world\n", at index 0. The
 * compiler will populate this from `OP_STR` operands as it
 * encounters string literals.
 *
 * The pool is intentionally NOT the same thing as the runtime heap.
 * Pool entries point to constant data in RODATA and never need to be
 * retained or released. A `T_STR` Value whose payload is a small pool
 * index (< STRING_POOL_SLOTS) refers to the pool; larger payloads
 * refer to heap-allocated strings.
 */
#ifndef SWIFTII_STRING_POOL_H
#define SWIFTII_STRING_POOL_H

#include <stdint.h>

typedef struct string_pool_entry {
  const char *data;
  uint16_t len;
} StringPoolEntry;

/* Look up an entry. Returns NULL if `idx` is out of range. */
const StringPoolEntry *string_pool_get(uint16_t idx);

#endif /* SWIFTII_STRING_POOL_H */
