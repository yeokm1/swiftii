/* Host-side storage for the ZP variables.
 *
 * cc65 reserves the actual zero-page slots ($D0-$DF) via
 * src/vm/zeropage.s. clang has no concept of "zero page", so on the
 * host build the same logical variables become ordinary globals
 * here. zeropage.h declares them; this file defines them.
 *
 * Excluded from the cc65 build via the __CC65__ guard — the asm
 * file is the single source of storage on the target.
 */
#ifndef __CC65__

#include <stdint.h>

uint16_t vm_pc;
uint8_t  vm_sp;
uint8_t  vm_fp;
uint16_t vm_tmp1;
uint16_t vm_tmp2;
uint16_t heap_ptr;
uint8_t  vm_op;
uint8_t  vm_flags;
uint16_t vm_tmp3;

#endif /* !__CC65__ */
