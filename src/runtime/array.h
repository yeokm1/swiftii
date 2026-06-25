/* Array runtime — heap-resident, growable, homogeneous arrays.
 *
 * See docs/contributing/design/007-arrays.md for the layout and growth rationale.
 * Heap payload format (after the standard 4-byte heap header):
 *
 *   byte 0,1:  count    u16  live element count
 *   byte 2,3:  capacity u16  allocated slot count (>= count)
 *   byte 4..:  slots    capacity × 3-byte tagged Value
 *
 * Per design doc 007 the growth policy is doubling on append-overflow
 * (initial capacity 4 for empty literals), with the old block becoming
 * dead heap bytes unless it was topmost (heap.c's LIFO reclaim
 * catches the topmost case). Element ownership belongs to the array:
 * `value_release` on a T_ARR whose refcount drops to zero calls
 * `array_release_elements` to release every live slot before the
 * heap block is reclaimed.
 *
 * The compiler emits OP_NEW_ARRAY for `[a, b, c]` literals; subscript
 * read/write and the `.count`, `.isEmpty`, `.append` methods are
 * Pratt postfix forms (`src/compiler/pratt.c`). Element-type homogen-
 * eity is enforced at the language level by the compiler
 * (the type checker); stores whatever values flow in and the
 * VM does no per-element type check.
 */
#ifndef SWIFTII_ARRAY_H
#define SWIFTII_ARRAY_H

#include <stdint.h>
#include "../common/types.h"
#include "../common/errors.h"
#include "heap.h"

/* Initial / minimum capacity for a freshly-allocated array. Picked at
 * 4 because doubling from there reaches the typical demo
 * sizes (8, 16, 32) in 1-3 reallocations. */
#define ARRAY_INIT_CAPACITY 4

/* Allocate a new array with `initial_capacity` slots reserved (and
 * count = 0). Returns the heap offset of the header, or HEAP_NULL on
 * out-of-memory. The caller wraps the returned offset in a T_ARR
 * Value with refcount 1 (set by heap_alloc). */
heap_off_t array_new(uint16_t initial_capacity);

/* Live element count. */
uint16_t array_count(heap_off_t arr);

/* Slot capacity. */
uint16_t array_capacity(heap_off_t arr);

/* Read element at `i` into `*out`. Returns SE_OK on success or
 * SE_RUNTIME if `i` is out of bounds (>= count). The caller is
 * responsible for retaining `*out` if it intends to keep the
 * reference past the next array mutation. */
swiftii_err_t array_get(heap_off_t arr, uint16_t i, Value *out);

/* Overwrite element at `i` with `*v`. Returns SE_OK on success or
 * SE_RUNTIME if `i` is out of bounds (>= count). Releases the slot's
 * previous value and retains `*v`. */
swiftii_err_t array_set(heap_off_t arr, uint16_t i, const Value *v);

/* Append `*v` to the array. May reallocate if capacity is full,
 * in which case `*arr_inout` is updated to the new heap offset.
 * Caller-provided `*v` is retained. Returns SE_OK or SE_OOM. */
swiftii_err_t array_append(heap_off_t *arr_inout, const Value *v);

#if defined(WITH_SWIFTSAT) || defined(WITH_SWIFTAUX) || defined(WITH_SWB) || \
    !defined(__CC65__)
/* Set the live element count to `n` (must be <= current count). Used by
 * the XLC array methods (removeLast / removeAll) — SWIFTSAT in-place,
 * SWIFTAUX via copy-down overlay, Family B Runner in normal CODE — to shrink
 * an array in place — the heap block and capacity are untouched, so the
 * offset is unchanged and no write-back is needed. Truncated slots keep
 * their raw bytes but are no longer "live", so a later
 * `array_release_elements` will not touch them: removeLast transfers the
 * popped element's refcount to the VM stack, removeAll releases first.
 * Gated out of lite (no caller there). */
void array_truncate(heap_off_t arr, uint16_t n);
#endif

/* Release every live element. Called by `value_release` when the
 * array's own refcount is about to drop to zero (the heap_release
 * call that frees the block follows). */
void array_release_elements(heap_off_t arr);

/* Initialise the first `n` slots from `src` (which lives on the VM
 * value stack) and set the array's count to `n`. Used by
 * `OP_NEW_ARRAY` for the literal `[a, b, c]` construction.
 * Ownership of the values transfers (no retain/release happens). */
void array_init_from_stack(heap_off_t arr, const Value *src, uint8_t n);

#endif /* SWIFTII_ARRAY_H */
