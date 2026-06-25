/* Static string pool — RODATA-resident compile-time string constants.
 *
 * Pool slot 0 is the hello-world entry, kept for the unit
 * test fixture in tests/unit/vm_test.c that runs the original
 * hand-assembled bytecode. The remaining slots are NULL placeholders;
 * Compile-time string literals also live on the heap (see
 * runtime/heap.c) rather than being interned here.
 *
 * A T_STR Value whose payload is < STRING_POOL_SLOTS refers to this
 * pool; larger payloads are heap byte offsets. heap.c initializes its
 * bump pointer to STRING_POOL_SLOTS so the discrimination is
 * unambiguous.
 */
#include "string_pool.h"

#include "../common/config.h"

static const char k_hello[] = "hello, world";

static const StringPoolEntry k_pool[1] = {
  { k_hello, sizeof(k_hello) - 1 }
};

const StringPoolEntry *string_pool_get(uint16_t idx) {
  if (idx >= (uint16_t)(sizeof(k_pool) / sizeof(k_pool[0]))) {
    return (const StringPoolEntry *)0;
  }
  return &k_pool[idx];
}
