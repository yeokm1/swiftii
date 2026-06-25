/* Tagged-Value operations.
 *
 * Adds the refcount helpers — value_retain/value_release inspect
 * the tag and the heap/pool discriminator (see docs/contributing/design/002) so that
 * the VM stack ops can manage ownership implicitly. Pool-resident
 * strings (payload < STRING_POOL_SLOTS) and immediate tags (T_INT, T_BOOL,
 * T_NIL, T_OPT_NIL) short-circuit to no-ops.
 */
#ifndef SWIFTII_VALUE_H
#define SWIFTII_VALUE_H

#include "../common/types.h"

/* Increment any owned heap reference held by `v`. No-op for immediates
 * and pool-resident strings. */
void value_retain(const Value *v);

/* Decrement any owned heap reference held by `v`. May reclaim the
 * underlying heap object if the count reaches zero and the block is
 * topmost. No-op for immediates and pool-resident strings. */
void value_release(const Value *v);


#endif /* SWIFTII_VALUE_H */
