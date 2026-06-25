/* Custom heap allocator with reference counting.
 *
 * A fixed-size BSS buffer with a bump-pointer allocator and a
 * LIFO last-block-release optimization. See docs/contributing/design/002-heap-and-strings.md.
 *
 * Each allocation has a 4-byte header (refcount u16, payload_len u16)
 * little-endian, followed by the payload bytes. A `heap_off_t` is the
 * byte offset of the header.
 *
 * Heap offsets always start at STRING_POOL_SLOTS (see common/config.h)
 * so that they are distinguishable from static-string-pool indices in
 * a T_STR Value payload. The bytes below STRING_POOL_SLOTS are wasted
 * on purpose.
 *
 * Added :mem reporting (via heap_free_bytes below). The
 * allocator may grow segregated free lists later if
 * real programs need them.
 *
 * Per docs/contributing/CONSTRAINTS.md rule 2 we never call malloc/free.
 */
#ifndef SWIFTII_HEAP_H
#define SWIFTII_HEAP_H

#include <stdint.h>

typedef uint16_t heap_off_t;

/* Sentinel returned by heap_alloc on out-of-memory. Picked as 0xFFFF so
 * it can never collide with a valid offset (HEAP_SIZE is < 0xFFFF). */
#define HEAP_NULL ((heap_off_t)0xFFFF)

/* Reset the heap to empty. Called by file_runner before each fresh
 * program compile; the REPL skips this so heap-resident compile-time
 * string constants persist across input lines. */
void heap_reset(void);

/* Allocate a payload of `payload_len` bytes. Returns the offset of the
 * header (refcount starts at 1) or HEAP_NULL on out-of-memory. */
heap_off_t heap_alloc(uint16_t payload_len);

/* Pointer to the first payload byte. Caller may read or write. The
 * pointer is invalidated by any subsequent heap_alloc / heap_release
 * that touches the same block. Returns NULL if `off` is out of range. */
unsigned char *heap_payload(heap_off_t off);

/* Payload length in bytes. Returns 0 if `off` is out of range. */
uint16_t heap_len(heap_off_t off);

/* Read-only peek at the current refcount. Returns 0 if `off` is out
 * of range. Used by `value_release` on T_ARR so it can call
 * `array_release_elements` exactly when the array's count is about
 * to drop to zero (the array layer doesn't see refcounts itself). */
uint16_t heap_refcount(heap_off_t off);

/* Increment refcount. No-op if `off` is out of range. */
void heap_retain(heap_off_t off);

/* Decrement refcount. If the count reaches zero AND the block is the
 * topmost allocation, the bump pointer rewinds to reclaim it.
 * Otherwise the block stays dead (refcount 0) and the bytes are
 * leaked until the next heap_reset. */
void heap_release(heap_off_t off);

/* Free bytes between the current bump pointer and HEAP_SIZE.
 * Used by the REPL `:mem` meta-command. */
uint16_t heap_free_bytes(void);

/* Snapshot the bump pointer so a failed compile can release every
 * compile-time heap allocation (string literals) in one shot. Pair
 * with `heap_rollback` on the parse-error path. */
heap_off_t heap_savepoint(void);

/* Restore the bump pointer to `saved`, discarding every allocation
 * made since. Safe only when the caller knows no live references
 * point at blocks above `saved` — used by the compiler's atomic
 * rollback since the only references to compile-time blocks live in
 * the (about to be discarded) bytecode buffer. */
void heap_rollback(heap_off_t saved);

/* (.swb) — the constant string-pool image. After compilation the
 * heap holds only compile-time string constants in [STRING_POOL_SLOTS,
 * bump). `heap_const_image` points `*out` at the first such byte (heap
 * offset STRING_POOL_SLOTS) and returns its length, so the compiler can
 * write it into a `.swb`; `heap_load_const` copies `len` bytes back to the
 * same offset and sets the bump pointer, so the runner reproduces the
 * exact byte layout (OP_STR offsets are array-relative, so they resolve
 * unchanged). Call `heap_reset()` before `heap_load_const`. */
uint16_t heap_const_image(const unsigned char **out);
void heap_load_const(const unsigned char *src, uint16_t len);

#endif /* SWIFTII_HEAP_H */
