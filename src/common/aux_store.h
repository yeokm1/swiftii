/* Shared aux-RAM bytecode store (//e Family B paging).
 *
 * The bytecode store lives in the //e auxiliary 64K (park at $2000), addressed
 * by absolute bytecode offset. Two consumers sit on it:
 *   - the Runner's read window  (src/vm/bcwin.c,    -DWITH_AUX_BC)
 *   - the Compiler's append-flush (src/compiler/bcbuf.c, -DWITH_AUX_COMPILE)
 * The //e Runner links both bcwin.c AND bcbuf.c, so the AUXMOVE param globals
 * and the C glue must be defined exactly once — here — not in each consumer.
 *
 * On cc65 these drive the ROM-AUXMOVE asm (src/platform/apple2/aux_bc.s); on
 * the host they copy to/from a test buffer so the paging logic is testable.
 */
#ifndef SWIFTII_AUX_STORE_H
#define SWIFTII_AUX_STORE_H

#if defined(WITH_AUX_BC) || defined(WITH_AUX_COMPILE)

#include <stdint.h>

void aux_store_read(uint16_t off, unsigned char *dst, uint16_t n);        /* store -> main */
void aux_store_write(uint16_t off, const unsigned char *src, uint16_t n); /* main -> store */

#if defined(BC_STORE_SATURN) && defined(__CC65__)
/* Tier 2 only: patch saturn_bc.s's bank-select/write-enable/cede switches for
 * the Saturn card in `slot` (0..7; $FF = none, a no-op). Call once at main()
 * entry, before the first aux_store_read/write. The aux (Tier 3) backend needs
 * no such init — ROM AUXMOVE is stateless. */
void __fastcall__ saturn_bc_init(unsigned char slot);
#endif

#ifndef __CC65__
/* Host-only: attach a writable buffer standing in for aux RAM (tests). */
void aux_store_host_attach(unsigned char *aux);
#endif

#endif /* WITH_AUX_BC || WITH_AUX_COMPILE */

#endif /* SWIFTII_AUX_STORE_H */
