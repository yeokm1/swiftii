/* Bytecode window — see bcwin.h. Entirely compiled out unless WITH_AUX_BC. */
#include "bcwin.h"

#ifdef WITH_AUX_BC

#include "../common/aux_store.h"   /* aux_store_read / aux_store_write */

unsigned char bcwin_buf[BC_WINDOW];
uint16_t bcwin_base;
uint16_t bcwin_len;
static uint16_t s_bc_total;

#define aux_read(off, dst, n)  aux_store_read((off), (dst), (n))
#define aux_write(off, src, n) aux_store_write((off), (src), (n))

/* ---- public API ---- */

void bcwin_begin(uint16_t bc_total) {
  s_bc_total = bc_total;
  bcwin_base = 0xFFFFu;   /* invalid: first ensure repages */
  bcwin_len = 0;
}

void bcwin_stage(uint16_t dst_off, const unsigned char *src, uint16_t n) {
  aux_write(dst_off, src, n);
}

void bcwin_ensure(uint16_t pc) {
  uint16_t avail;
  uint16_t win_end = (uint16_t)(bcwin_base + bcwin_len);

  /* Repage when pc is below the window (backward jump / call return) or the
   * 3-byte instruction would run past the window AND there's more image to
   * the right (i.e. the window isn't already capped by end-of-bytecode). */
  if (pc >= bcwin_base &&
      (pc + 3u <= win_end || win_end >= s_bc_total)) {
    return;  /* already mapped */
  }

  avail = (uint16_t)(s_bc_total - pc);
  bcwin_len = (avail < BC_WINDOW) ? avail : (uint16_t)BC_WINDOW;
  aux_read(pc, bcwin_buf, bcwin_len);
  bcwin_base = pc;
}

#endif /* WITH_AUX_BC */
