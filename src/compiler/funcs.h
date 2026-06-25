/* Function symbol table.
 *
 * Each entry maps a function name (Swift identifier) to an absolute
 * bcbuf offset and a signature summary (param_count, has_return).
 * The compiler writes entries during `func` declarations; the VM
 * reads them at `OP_CALL` dispatch to find the call target. Both
 * the compile-time symbol table and the runtime address lookup are
 * served by the single table — there is no separate "function index
 * → address" table at runtime.
 *
 * Names are stored zero-padded to IDENT_MAX bytes, matching the
 * globals table convention. Lookup is exact-length-and-bytes; the
 * compiler errors on collisions.
 *
 * Persistence:
 *   - File mode: `funcs_reset()` is called by file_runner before each
 *     fresh compile (alongside `globals_reset()` and `heap_reset()`),
 *     so the table starts empty.
 *   - REPL mode: the table persists across input lines so functions
 *     defined earlier stay callable. `:reset` clears it.
 *
 * Atomic-compile rollback: `funcs_savepoint`/`funcs_rollback` snapshot
 * and restore the entry count so a parse error mid-`func` declaration
 * leaves the table in its pre-statement state, the same way globals
 * and heap do.
 */
#ifndef SWIFTII_FUNCS_H
#define SWIFTII_FUNCS_H

#include <stdint.h>
#include "../common/config.h"   /* SWIFTII_FUNC_REDEF */
#include "../common/ctype.h"

/* Hard cap on stored per-parameter compile-time types. Demos
 * never exceed 4; six leaves headroom. Functions declaring more
 * parameters than this are still legal up to MAX_LOCALS but the
 * tracker stops recording per-param ctypes past slot 5 (validation
 * silently falls back to CT_UNKNOWN, i.e. accept-anything) — see
 * docs/contributing/design/009-type-tracker.md § Cost. */
#define FUNC_MAX_TRACKED_PARAMS 6

/* Drop every function entry. Also rewinds the bcbuf arena via
 * `bcbuf_arena_reset()` — the persistent function bytecode and the
 * function table are conceptually one piece of state. */
void funcs_reset(void);

/* Number of functions currently defined (0..MAX_FUNCS). */
uint8_t funcs_count(void);

/* Find `name` (length `len`) in the table. Returns the index (0..) or
 * a negative value if not found. */
int16_t funcs_find(const char *name, uint16_t len);

/* Reserve a new function entry by name. Returns the new index, or a
 * negative value on out-of-space or duplicate name. The entry's
 * bytecode offset and signature must be filled in via
 * `funcs_finalize` once the body is compiled. */
int16_t funcs_declare(const char *name, uint16_t len);

/* (.swb) — append a runtime-only function entry from a
 * deserialised image: the runner has no source to compile, so it rebuilds
 * the call table directly from `{bc_start, param_count, has_return}` (the
 * only fields the VM reads — name + ctypes are compile-only and stay
 * behind). Call in index order 0..n-1 after `funcs_reset()`. Returns the
 * new index or a negative value if the table is full. */
int16_t funcs_add_runtime(uint16_t bc_start, uint8_t param_count,
                          unsigned char has_return);

/* Record the parameter count, return-shape, and return ctype for a
 * function. Call this AFTER parsing the parameter list (so recursive
 * calls inside the body see the right arity) and BEFORE compiling the
 * body. `ret_ctype` is the declared return type, or CT_VOID when
 * `has_return == 0`. Per-parameter ctypes are set separately via
 * `funcs_set_param_ctype` during parameter parsing. */
void funcs_set_signature(uint8_t fn_idx, uint8_t param_count,
                         unsigned char has_return, ctype_t ret_ctype);

/* Record one parameter's declared compile-time type. `param_idx` is
 * the slot position (0..param_count-1). Indices >= FUNC_MAX_TRACKED_PARAMS
 * are silently ignored — validation falls back to CT_UNKNOWN. */
void funcs_set_param_ctype(uint8_t fn_idx, uint8_t param_idx, ctype_t ctype);

/* Return the declared compile-time type of a parameter, or CT_UNKNOWN
 * for out-of-range / not-tracked slots (param_idx >=
 * FUNC_MAX_TRACKED_PARAMS, or fn_idx out of range). */
ctype_t funcs_get_param_ctype(uint8_t fn_idx, uint8_t param_idx);

/* Return the declared return type, or CT_VOID for a void function /
 * out-of-range index. */
ctype_t funcs_get_ret_ctype(uint8_t fn_idx);

/* Record the body's bytecode offset for a function. Call this AFTER
 * the body has been compiled and rotated into the arena. */
void funcs_set_start(uint8_t fn_idx, uint16_t bc_start);

/* Address lookup for OP_CALL. Returns the absolute bcbuf offset of
 * the function's body start, or 0xFFFF if `fn_idx` is out of range.
 * Used by `vm.c`. */
uint16_t funcs_get_start(uint8_t fn_idx);

/* Arity lookup for OP_CALL. Returns the declared parameter count,
 * or 0xFF if `fn_idx` is out of range. Used by `vm.c` to bounds-check
 * the call site (which carries argc as an inline u8). */
uint8_t funcs_get_param_count(uint8_t fn_idx);

/* Returns 1 if the function at `fn_idx` was declared with a `->` return
 * annotation (i.e., must use OP_RETURN rather than OP_RETURN_V), else 0. */
unsigned char funcs_has_return(uint8_t fn_idx);

#if SWIFTII_FUNC_REDEF
/* REPL function redefinition (//e REPL binaries only). Before
 * `parse_func_decl` rebinds an existing slot to a freshly compiled body,
 * it calls this to snapshot the slot's mutable fields (bc_start, param
 * count, has_return, return ctype) so a failed compile can be rolled back
 * to the prior definition via `funcs_rollback`. One slot is tracked; it is
 * cleared at each `funcs_savepoint`. */
void funcs_begin_replace(uint8_t fn_idx);
#endif

/* Snapshot the current function count so a failed compile can roll
 * back to it. */
void funcs_savepoint(uint8_t *count_out);

/* Drop every function whose index is >= `saved_count`. The bcbuf
 * arena is rewound by the caller via `bcbuf_arena_reset` only when
 * required — we cannot rewind the arena to the savepoint because
 * arena bytes from earlier successful function declarations live
 * below the savepoint. Instead the caller (compiler.c) tracks the
 * arena watermark at savepoint time and rewinds explicitly. */
void funcs_rollback(uint8_t saved_count);

#endif /* SWIFTII_FUNCS_H */
