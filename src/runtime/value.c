/* Tagged-Value operations.
 *
 * Refcount helpers shared by the VM dispatch loop. The
 * pool/heap discriminator (payload < STRING_POOL_SLOTS) is enforced
 * here so all stack ops agree on what "owns" a heap reference. */
#include "value.h"

#include "../common/config.h"
#include "../common/types.h"
#include "array.h"
#include "heap.h"

void value_retain(const Value *v) {
  unsigned char t = v->tag;
  uint16_t p;
  if (t == T_STR) {
    p = VALUE_PAYLOAD_U16(*v);
    if (p >= (uint16_t)STRING_POOL_SLOTS) {
      heap_retain((heap_off_t)p);
    }
  } else if (t == T_ARR) {
    heap_retain((heap_off_t)VALUE_PAYLOAD_U16(*v));
  }
}

void value_release(const Value *v) {
  unsigned char t = v->tag;
  uint16_t p;
  heap_off_t off;
  if (t == T_STR) {
    p = VALUE_PAYLOAD_U16(*v);
    if (p >= (uint16_t)STRING_POOL_SLOTS) {
      heap_release((heap_off_t)p);
    }
  } else if (t == T_ARR) {
    off = (heap_off_t)VALUE_PAYLOAD_U16(*v);
#ifndef NO_ARRAY_RUNTIME
    /* If this is the last reference, release every live element
     * before the heap block is reclaimed. heap_refcount peek lets
     * us decide without baking the array-aware logic into heap.c.
     * NO_ARRAY_RUNTIME (the Family B Compiler) never creates a heap
     * array — arrays are emitted as opcodes, not compile-time constants
     * — so this path is dead there, and skipping it lets the Compiler
     * drop array.c entirely (~1.5 KB; see Makefile COMPILER_SRC). */
    if (heap_refcount(off) == 1) {
      array_release_elements(off);
    }
#endif
    heap_release(off);
  }
}
