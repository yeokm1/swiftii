/* Shared bytecode buffer with split arena / scratch layout.
 *
 * The REPL and the file runner never run concurrently — a SwiftII
 * process is either one or the other. Before they each owned
 * a private FILE_BC_SIZE-byte buffer, doubling the BSS cost. This
 * module holds the one buffer they share, and partitions it:
 *
 *   bcbuf[0 .. arena_used)                         function arena
 *   bcbuf[arena_used .. arena_used + scratch_used) top-level scratch
 *
 * The arena holds compiled function bodies. In REPL mode it persists
 * across input lines so a function defined on one line is callable
 * on the next; `globals_reset()` (file mode, REPL `:reset`) also
 * clears it via `bcbuf_arena_reset()`. The scratch is reset before
 * each compile by initialising the parser's bytecode cursor to
 * `arena_used`; nothing in this module enforces that, but the
 * compiler always overwrites the previous scratch.
 *
 * When the compiler encounters a `func` declaration, it emits the
 * body at the current end-of-scratch (so any preceding top-level
 * scratch bytes already emitted on the same line stay put), then
 * calls `bcbuf_rotate_func_into_arena()` to slide the body down to
 * the front and shift the preceding scratch bytes up. The three-
 * reversal rotation preserves bytes verbatim — relative JUMP /
 * LOOP / IF_FALSE offsets stay valid because they were emitted
 * against PC and the rotation does not change body-internal or
 * scratch-internal distances. (See `funcs.c` for fn-table
 * housekeeping; `OP_CALL` targets are looked up by fn_idx, not by
 * absolute address baked into bytecode, so the rotation does not
 * require any bytecode patching.)
 */
#ifndef SWIFTII_BCBUF_H
#define SWIFTII_BCBUF_H

#include <stdint.h>

/* Pointer to the start of the shared buffer (offset 0). */
unsigned char *bcbuf_data(void);

/* Total buffer capacity in bytes. */
uint16_t bcbuf_size(void);

/* Bytes currently committed to the function arena.
 * 0..bcbuf_size(); top-level scratch starts here. */
uint16_t bcbuf_arena_used(void);

/* Clear the function arena. Called from `globals_reset()` so that
 * file-runner's "fresh program" and REPL `:reset` both wipe persistent
 * function bytecode along with the function and global symbol tables. */
void bcbuf_arena_reset(void);

/* Rewind the arena watermark to `new_used`. Caller invariant:
 * `new_used <= bcbuf_arena_used()`. Used by the compiler's atomic-
 * rollback path so a failed compile that emitted function bodies
 * into the arena (before tripping a later parse error) doesn't leak
 * arena bytes. */
void bcbuf_arena_truncate(uint16_t new_used);

/* Rotate `func_size` bytes living at offset `func_start ..
 * func_start + func_size` to the front of the arena. The bytes
 * currently at `[arena_used .. func_start)` (top-level scratch already
 * emitted earlier on this line/file) shift up by `func_size`.
 *
 * Returns the function's new start offset (always equals the old
 * `arena_used`). On success, `arena_used` is bumped by `func_size`.
 * Returns 0xFFFF if the requested region is out of range; the caller
 * should fail the compile in that case.
 *
 * Caller invariant: func_start >= arena_used and
 *                    func_start + func_size <= bcbuf_size().
 */
uint16_t bcbuf_rotate_func_into_arena(uint16_t func_start,
                                      uint16_t func_size);

/* ---- //e Compiler bytecode paging (-DWITH_AUX_COMPILE) ----
 *
 * The buffer becomes a MAIN window over absolute bytecode offsets; completed
 * (immutable) function bodies are flushed to the aux store as the arena grows,
 * so only the in-progress function + the still-mutable top-level scratch stay
 * resident. Callers address bytecode by ABSOLUTE offset through BC_PUT/BC_GET;
 * cursors (bc_pos, func_start, funcs bc_start) stay absolute and travel in the
 * `.swb` unchanged. Compiled out entirely without the flag — BC_PUT/BC_GET are
 * then the verbatim flat `p->bc[i]` accesses, so non-paged builds are
 * byte-identical. See docs + LESSONS for the append-only-flush rationale. */
#ifdef WITH_AUX_COMPILE
/* Logical bytecode ceiling (the aux park capacity, $2000..~$B000 aux). The
 * parser's bc_cap is set to this, not the small MAIN window; the window-full
 * case is reported separately by bcbuf_put. Overridable via -D. */
#ifndef AUX_BC_MAX
#define AUX_BC_MAX 36864u
#endif

/* Write byte `b` at absolute offset `abs` (an append at the scratch frontier,
 * or a backpatch into resident scratch). Flushes the frozen arena prefix to
 * aux if the window is full. Returns 1 on success, 0 if the resident scratch
 * alone overflows the window (program too big for this machine). */
int bcbuf_put(uint16_t abs, unsigned char b);
/* Read the byte at absolute offset `abs`. Only the resident region is read
 * (the compiler never reads flushed/frozen bytecode). */
unsigned char bcbuf_get(uint16_t abs);
/* Bytes already flushed to the aux store, i.e. the absolute offset of the
 * first byte still resident in the window. swb_write streams [0..this) from
 * aux and [this..bc_len) from bcbuf_data(). */
uint16_t bcbuf_flushed(void);

#define BC_PUT(p, i, b) bcbuf_put((uint16_t)(i), (b))
#define BC_GET(p, i)    bcbuf_get((uint16_t)(i))
#else
#define BC_PUT(p, i, b) ((p)->bc[(i)] = (b))
#define BC_GET(p, i)    ((p)->bc[(i)])
#endif

#endif /* SWIFTII_BCBUF_H */
