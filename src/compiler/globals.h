/* Global symbol table.
 *
 * Stores one entry per `let`/`var` defined at top level. Names are
 * stored zero-padded to IDENT_MAX bytes (IDENT_MAX-1 significant chars);
 * declaring a longer identifier is a compile-time error (the parser's
 * expect_decl_name reports it; globals_define also rejects it defensively),
 * per docs/using/LANGUAGE.md.
 *
 * The table is preserved across REPL inputs (so `let x = 1` followed by
 * `print(x)` on the next line works) and reset by file_runner between
 * fresh program compiles via `globals_reset()`.
 */
#ifndef SWIFTII_GLOBALS_H
#define SWIFTII_GLOBALS_H

#include <stdint.h>
#include "../common/ctype.h"

/* Drop every entry. Called by file_runner before each fresh compile;
 * the REPL skips this so globals persist across input lines. */
void globals_reset(void);

/* Number of globals currently defined (0..MAX_GLOBALS). */
uint8_t globals_count(void);

/* Find `name` (length `len`) in the table. Returns the index (0..) or
 * a negative value if not found. */
int16_t globals_find(const char *name, uint16_t len);

/* Define a new global. `ctype` is the compile-time type; pass
 * CT_UNKNOWN when the type is not yet inferred (commit 3 wires Pratt
 * propagation to fill it in). Returns the new index, or a negative
 * value on out-of-space or duplicate name. */
int16_t globals_define(const char *name, uint16_t len, unsigned char is_let,
                       ctype_t ctype);

/* Returns 1 if the global at `index` was declared with `let`, else 0. */
unsigned char globals_is_let(uint8_t index);

/* Read / update the compile-time type of an existing global. Out-of-
 * range index returns CT_UNKNOWN / silently no-ops. */
ctype_t globals_get_ctype(uint8_t index);
void globals_set_ctype(uint8_t index, ctype_t ctype);

/* Returns a pointer to the zero-padded name buffer (length IDENT_MAX) of
 * the global at `index`, or NULL if out of range. The buffer is owned by
 * the symbol table — callers must not mutate it. Names that fill the
 * buffer have no trailing NUL within the IDENT_MAX bytes. */
const char *globals_get_name(uint8_t index);

/* Snapshot the current global count so a failed compile can roll back.
 * cc65 warns (and emits suboptimal code) on by-value struct passing of
 * any size, so we use a scalar out-pointer. Used by the REPL compile
 * path so a parse error mid-statement (e.g. `let y = 2.4` where `.4`
 * makes the terminator check fail after `y` was already added) doesn't
 * leave a half-defined name in the table. */
void globals_savepoint(uint8_t *count_out);

/* Drop every global whose index is >= `saved_count`. Zeros dropped
 * name buffers so subsequent definitions reuse the slot cleanly. */
void globals_rollback(uint8_t saved_count);

#endif /* SWIFTII_GLOBALS_H */
