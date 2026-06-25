/* Shared aux-RAM bytecode store — see aux_store.h. */
#include "aux_store.h"

#if defined(WITH_AUX_BC) || defined(WITH_AUX_COMPILE)

#ifdef __CC65__
/* Param transport for the asm store driver (the xlc_argc convention): set
 * these, then call the no-arg worker. `off` is relative to the store's park
 * base. Shared by both backends; only the worker symbols differ. */
uint16_t bc_aux_off;
unsigned char *bc_aux_ptr;
uint16_t bc_aux_n;

#ifdef BC_STORE_SATURN
/* Tier 2 (II+ Saturn 128K): bytecode parked in Saturn banks ($D000 window).
 * The driver (saturn_bc.s) banks the Saturn LC region in/out around each copy
 * and restores ProDOS's MLI bank afterwards. saturn_bc_init() must run once at
 * main() entry with the detected Saturn slot before any store access. */
extern void store_page(void);     /* store -> main (saturn_bc.s) */
extern void store_stage(void);    /* main -> store (saturn_bc.s) */
#else
/* Tier 3 (//e aux 64K): bytecode parked at aux $2000 via ROM AUXMOVE. */
extern void aux_bc_page(void);    /* aux -> main */
extern void aux_bc_stage(void);   /* main -> aux */
#define store_page  aux_bc_page
#define store_stage aux_bc_stage
#endif

void aux_store_read(uint16_t off, unsigned char *dst, uint16_t n) {
  bc_aux_off = off; bc_aux_ptr = dst; bc_aux_n = n;
  store_page();
}
void aux_store_write(uint16_t off, const unsigned char *src, uint16_t n) {
  bc_aux_off = off; bc_aux_ptr = (unsigned char *)src; bc_aux_n = n;
  store_stage();
}
#else
static unsigned char *s_host_aux;
void aux_store_host_attach(unsigned char *aux) { s_host_aux = aux; }

void aux_store_read(uint16_t off, unsigned char *dst, uint16_t n) {
  uint16_t i;
  for (i = 0; i < n; ++i) dst[i] = s_host_aux[off + i];
}
void aux_store_write(uint16_t off, const unsigned char *src, uint16_t n) {
  uint16_t i;
  for (i = 0; i < n; ++i) s_host_aux[off + i] = src[i];
}
#endif

#endif /* WITH_AUX_BC || WITH_AUX_COMPILE */
