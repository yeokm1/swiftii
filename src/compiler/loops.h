/* Loop-context tracking for `break` statements.
 *
 * The compiler maintains a stack of nested-loop contexts during
 * parsing. When a `while` or `for-in` body starts, `loops_enter()`
 * pushes an empty context. Each `break` inside that body emits an
 * `OP_JUMP <placeholder>` and calls `loops_record_break_site()` to
 * register the placeholder. When the body closes, `loops_exit()`
 * patches every recorded placeholder to point past the loop's exit
 * (i.e. to the current emit position).
 *
 * `break` outside any loop body is a compile error — callers query
 * `loops_in_loop()` before recording.
 *
 * Limits sized demos: 4 nested loops, up to 4 breaks
 * per loop (the latter is rarely exceeded — most loops have a single
 * early-exit). The fixed caps avoid heap allocation; total BSS cost
 * is 4 × 4 × 2 + 4 + 1 = 37 bytes.
 */
#ifndef SWIFTII_LOOPS_H
#define SWIFTII_LOOPS_H

#include <stdint.h>
#include "parser.h"

#define LOOPS_MAX_DEPTH        4
#define LOOPS_MAX_BREAKS_LOOP  4

/* Drop all loop contexts. Called by the compiler at the start of
 * every fresh compile (not per REPL line) since loops are syntactic
 * and never span statements at the parser layer. */
void loops_reset(void);

/* 1 if currently inside at least one loop body, else 0. */
unsigned char loops_in_loop(void);

/* Push a new (empty) loop context. Returns 0 on success, -1 if the
 * nesting limit is exceeded. */
int loops_enter(void);

/* Record a `break` placeholder offset within the current (innermost)
 * loop. Returns 0 on success, -1 if too many breaks in this loop or
 * not currently in a loop. */
int loops_record_break_site(uint16_t placeholder_pos);

/* Pop the current loop context, patching every recorded break site
 * to jump to the current `p->bc_pos` (the byte right after the loop
 * exit). No-op if no loop is on the stack — caller is expected to
 * call this exactly once per matching `loops_enter()`. */
void loops_exit(Parser *p);

#endif /* SWIFTII_LOOPS_H */
