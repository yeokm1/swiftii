/* Builtin functions exposed to user code via OP_CALL_BUILTIN.
 *
 * Only ships `print` (id BUILTIN_PRINT). Adds
 * `readLine`, `Int`, `String`, etc.
 */
#ifndef SWIFTII_BUILTINS_H
#define SWIFTII_BUILTINS_H

#include <stdint.h>

#include "../common/types.h"
#include "../common/ctype.h"
#include "../common/errors.h"

/* Print a single Value. Strings are looked up in the string pool;
 * other tags fall through to a placeholder adds
 * proper formatting. */
void builtins_print_value(const Value *v);

/* Print a newline. */
void builtins_print_newline(void);

/* random(in:) worker — Phase 16 stretch. Writes an Int in [a, b)
 * (closed == 0) or [a, b] (closed == 1) into *out; SE_RUNTIME on an empty
 * range. 16-bit xorshift; each draw folds in platform_entropy() (keypress
 * timing on the Apple II, 0 on the host -> host stays deterministic). The
 * DEFINITION is
 * gated to WITH_BIGLANG builds (host + Family B); this declaration is inert
 * elsewhere. Shared by the vm.c inline dispatch (Runner) and
 * xlc_call_builtin_dispatch (host) so there's one RNG, not two. */
swiftii_err_t builtin_random_in(int16_t a, int16_t b, unsigned char closed,
                                Value *out);

/* builtins_type_name + builtins_write_ctype removed 2026-06-06 — `:list`
 * (their only caller) dropped its `: Type` annotation; see builtins.c. */

#endif /* SWIFTII_BUILTINS_H */
