/* VM dispatch entry point.
 *
 * Full integer + bool + nil + string-constant value stack, with
 * global variable storage shared across `vm_run` calls (REPL persistence).
 * Adds call frames and locals; swaps the C dispatch loop
 * for hand-tuned 6502.
 */
#ifndef SWIFTII_VM_H
#define SWIFTII_VM_H

#include <stdint.h>
#include "../common/types.h"
#include "../common/errors.h"

/* Run bytecode at `code` until OP_HALT or error. Execution starts at
 * offset `start_pc`; `len` is the total number of valid bytes in the
 * buffer (`bcbuf_arena_used()` + scratch). The bounds check uses
 * `len` rather than `start_pc + scratch_size` because `OP_CALL` may
 * jump into the function arena at offsets below `start_pc`.
 *
 * Added `start_pc` so the VM can begin execution at the top
 * of the scratch region (after the persistent function arena).
 * Globals persist across calls; the value stack and call stack are
 * reset on entry. */
swiftii_err_t vm_run(const unsigned char *code, uint16_t start_pc,
                     uint16_t len);

/* Clear global variable storage. Called by file_runner before each fresh
 * compile; the REPL skips this so globals persist across input lines. */
void vm_reset_globals(void);

/* Copy the current value of global `index` into `*out`. Returns 1 on
 * success, 0 if `index` is out of range or undefined. */
unsigned char vm_get_global(uint8_t index, Value *out);

#if defined(WITH_SWIFTSAT) || defined(WITH_SWIFTAUX) || defined(WITH_SWB) || \
    !defined(__CC65__)
/* Shared string helpers, exposed (dropped `static`) so XLC-resident
 * opcode dispatchers in builtins_xlc.c can call back into the single
 * MAIN-resident copy rather than duplicating the logic. Both stay
 * physically in MAIN CODE (vm.o has no XLC pragma); the XLC caller
 * reaches them by a plain JSR into $2xxx, which is always visible
 * regardless of the Saturn bank-select / aux copy-down state. Declared
 * only on builds that have an XLC path (SWIFTSAT, SWIFTAUX, host); lite keep them file-local. */

/* Read a string's bytes + length whether it lives in the pool or on
 * the heap. Out-of-range strings come back with len 0 / NULL data. */
void str_bytes(uint16_t payload, const unsigned char **data,
               uint16_t *len);

/* Allocate a heap string of `n` bytes, copy from `src`, and write a
 * T_STR Value to `*out`. Returns SE_OOM on heap exhaustion. */
swiftii_err_t make_heap_str(const unsigned char *src, uint16_t n,
                            Value *out);

/* Format an int16 into `buf` (no NUL); returns the byte count. `buf`
 * must hold >= 7 bytes (-32768 plus sign). */
uint16_t fmt_i16(int16_t n, unsigned char *buf);
#endif

#endif /* SWIFTII_VM_H */
