/* Bytecode window — paged bytecode for the Family B Runner (-DWITH_AUX_BC).
 *
 * The VM is a jump machine: it reads bytecode at arbitrary PCs, so the
 * whole program normally has to be byte-addressable in MAIN at once (the
 * `s_image` buffer in runner_main.c), which caps program size. On a //e
 * the auxiliary 64 KB is otherwise unused by the MAIN-only Runner, so we
 * park the full bytecode there and keep only a small fixed MAIN window of
 * it live, paging a fresh window in whenever the PC leaves the current one
 * (a jump, a call, or a loop running off the window's end). Sequential
 * execution inside a window costs nothing; only control-flow that leaves
 * the window triggers a copy.
 *
 * Constant pool, funcs table, and the runtime heap stay MAIN-resident —
 * only the bytecode is paged (see docs/design + the plan).
 *
 * This header is included unconditionally but only does anything when
 * WITH_AUX_BC is defined; vm.c selects the windowed access path under the
 * same flag, so a non-paged build (II+ Runner, host) is byte-identical.
 */
#ifndef SWIFTII_BCWIN_H
#define SWIFTII_BCWIN_H

#include <stdint.h>

#ifdef WITH_AUX_BC

#ifndef BC_WINDOW
#define BC_WINDOW 1024u
#endif

/* The live window and its image-offset base. Defined in bcwin.c; vm.c
 * reads them directly in the hot path (BC_AT) to avoid a call per byte. */
extern unsigned char bcwin_buf[BC_WINDOW];
extern uint16_t bcwin_base;   /* image offset of bcwin_buf[0] */
extern uint16_t bcwin_len;    /* valid bytes currently in bcwin_buf */

/* Begin a run over a `bc_total`-byte bytecode image already staged into
 * the backing store (aux). Invalidates the window so the first access
 * repages. */
void bcwin_begin(uint16_t bc_total);

/* Ensure bytes [pc, pc+2] (one whole instruction, max 3 bytes) are in the
 * window, repaging from the backing store if not. The end-of-image case
 * (fewer than 3 bytes remain) is handled: the VM's own bounds checks stop
 * it reading past bc_total. */
void bcwin_ensure(uint16_t pc);

/* Stage `n` bytecode bytes from MAIN `src` into the backing store at image
 * offset `dst_off`. Called by the Runner's load path while streaming the
 * `.swb` bytecode section in; never lands the whole image in MAIN. */
void bcwin_stage(uint16_t dst_off, const unsigned char *src, uint16_t n);

/* The host backing buffer is attached via aux_store_host_attach()
 * (src/common/aux_store.h) — shared with the Compiler's paging. */

#endif /* WITH_AUX_BC */

#endif /* SWIFTII_BCWIN_H */
