/* Local-variable symbol table (compile-time only).
 *
 * Per `clox` Chapter 22 conventions, locals are tracked by the compiler
 * as a flat array of {name, depth} entries. Each entry's array index
 * is also its stack-frame offset: when compiling `let x = expr` inside
 * a function body, we (a) parse `expr` to leave the value on the VM
 * stack, (b) call `locals_declare(name)` which records the name at
 * slot offset = (current local count - 1). The slot is naturally on
 * the stack at frame_base + offset, so emitting `OP_GET_LOCAL i` later
 * reads from the right place — no separate OP_DEFINE_LOCAL needed.
 *
 * Supports a single, flat scope per function: function
 * parameters occupy slots 0..param_count, and subsequent `let`/`var`
 * declarations append. Nested block scoping (with locals going out of
 * scope at `}`) is deferred (alongside `if let` in
 * function bodies, which is the first feature that needs it).
 * Meanwhile, locals
 * live until the function returns. This matches the demo audience
 * (small recursive functions) without the bookkeeping for scope-pop.
 *
 * The compiler resets the table on entering each function body via
 * `locals_reset()`. Outside any function, `locals_count()` is zero
 * and `locals_find()` always returns -1, so identifier resolution
 * falls through to globals.
 */
#ifndef SWIFTII_LOCALS_H
#define SWIFTII_LOCALS_H

#include <stdint.h>
#include "../common/ctype.h"

/* Clear the locals table. Called on function-body entry. */
void locals_reset(void);

/* Current local count (0..MAX_LOCALS). After function-body entry +
 * parameter declaration, this equals the parameter count. */
uint8_t locals_count(void);

/* Find `name` (length `len`) among current locals. Returns the slot
 * offset (0..MAX_LOCALS-1) or a negative value if not present. */
int16_t locals_find(const char *name, uint16_t len);

/* Append a new local with `name`. `ctype` is the compile-time type;
 * pass CT_UNKNOWN when not yet inferred. Returns the new slot offset,
 * or a negative value on out-of-space or duplicate-name collision. */
int16_t locals_declare(const char *name, uint16_t len, unsigned char is_let,
                       ctype_t ctype);

/* Drop locals back to `n` entries (no-op if `n` is already >= the
 * current count). Used to pop a block scope — e.g. the `if let`
 * binding and any locals declared inside its body go out of scope at
 * the closing `}`. The caller is responsible for emitting the matching
 * OP_POPs so the VM stack stays balanced with the compile-time count. */
void locals_truncate(uint8_t n);

/* Returns 1 if the local at `index` was declared with `let`, else 0.
 * Out-of-range index returns 0 (treat as `var`-like; the caller is
 * expected to validate the index first). */
unsigned char locals_is_let(uint8_t index);

/* Read / update the compile-time type of an existing local. Out-of-
 * range index returns CT_UNKNOWN / silently no-ops. */
ctype_t locals_get_ctype(uint8_t index);
void locals_set_ctype(uint8_t index, ctype_t ctype);

#endif /* SWIFTII_LOCALS_H */
