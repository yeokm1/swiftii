/* Integer arithmetic runtime helpers.
 *
 * Puts mul/div/mod here (rather than inlining in vm.c) because:
 *  - The 6502 has no multiply/divide instructions; cc65 emits a function
 *    call to a runtime routine anyway, so keeping these in a separate
 *    compilation unit costs nothing.
 * - the optimization plan will move the dispatch loop into a hot
 *    code segment. These helpers live in cold code by default; isolating
 *    them now keeps that future split simple.
 *
 * All operations are 16-bit signed with wrap-around on overflow. Division
 * and modulo report SE_DIV_ZERO when the divisor is zero; otherwise they
 * defer to C semantics (truncate toward zero, sign of remainder follows
 * dividend on cc65 and clang alike for the values in our range).
 */
#include <stdint.h>
#include "../../common/errors.h"

int16_t int_mul(int16_t a, int16_t b) {
  return (int16_t)((uint16_t)a * (uint16_t)b);
}

swiftii_err_t int_div(int16_t a, int16_t b, int16_t *out) {
  if (b == 0) return SE_DIV_ZERO;
  *out = (int16_t)(a / b);
  return SE_OK;
}

swiftii_err_t int_mod(int16_t a, int16_t b, int16_t *out) {
  if (b == 0) return SE_DIV_ZERO;
  *out = (int16_t)(a % b);
  return SE_OK;
}
