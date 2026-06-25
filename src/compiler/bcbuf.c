/* Shared bytecode buffer.
 *
 * See bcbuf.h for the rationale and arena/scratch layout. The buffer
 * is FILE_BC_SIZE bytes — cc65 puts it in BSS automatically since
 * there's no initialiser.
 *
 * With -DWITH_AUX_COMPILE (the //e Family B Compiler) the same s_buf is a
 * MAIN *window* over absolute bytecode offsets: as functions complete and
 * rotate into the arena they become immutable and are never re-read by the
 * compiler (OP_CALL resolves by fn index, not address), so the frozen arena
 * prefix is flushed to the aux store and only the in-progress function + the
 * still-mutable top-level scratch stay resident. Without the flag this file is
 * the plain flat buffer, byte-identical to before.
 */
#include "bcbuf.h"

#include "../common/config.h"
#ifdef WITH_AUX_COMPILE
#include "../common/aux_store.h"
#endif

static unsigned char s_buf[FILE_BC_SIZE];
static uint16_t      s_arena_used;
#ifdef WITH_AUX_COMPILE
static uint16_t      s_win_base;   /* absolute offset of s_buf[0]; bytes
                                      [0..s_win_base) are flushed to aux */
#endif

unsigned char *bcbuf_data(void) {
  return s_buf;
}

uint16_t bcbuf_size(void) {
  return (uint16_t)FILE_BC_SIZE;
}

uint16_t bcbuf_arena_used(void) {
  return s_arena_used;
}

void bcbuf_arena_reset(void) {
  s_arena_used = 0;
#ifdef WITH_AUX_COMPILE
  s_win_base = 0;
#endif
}

void bcbuf_arena_truncate(uint16_t new_used) {
  if (new_used > s_arena_used) return;
  s_arena_used = new_used;
  /* WITH_AUX_COMPILE note: the Compiler is file-one-shot, so a parse-error
   * rollback below s_win_base (already-flushed functions) is followed by a
   * fresh bcbuf_arena_reset for the next program — we don't un-flush aux. */
}

/* Reverse the byte range buf[lo..hi). Standard in-place reversal. Indices are
 * physical s_buf offsets (window-relative when WITH_AUX_COMPILE). */
static void reverse_range(unsigned char *buf, uint16_t lo, uint16_t hi) {
  unsigned char t;
  while (lo + 1 < hi) {
    --hi;
    t = buf[lo];
    buf[lo] = buf[hi];
    buf[hi] = t;
    ++lo;
  }
}

#ifndef WITH_AUX_COMPILE

uint16_t bcbuf_rotate_func_into_arena(uint16_t func_start,
                                      uint16_t func_size) {
  uint16_t lo;
  uint16_t hi;
  uint16_t new_start;

  lo = s_arena_used;
  hi = (uint16_t)(func_start + func_size);

  /* Bounds and ordering checks. */
  if (func_start < lo) return (uint16_t)0xFFFF;
  if (hi > (uint16_t)FILE_BC_SIZE) return (uint16_t)0xFFFF;
  if (func_start > hi) return (uint16_t)0xFFFF;

  /* Three-reversal rotation: rotate buf[lo..hi) so the last `func_size`
   * bytes (currently at [func_start..hi)) move to the front, and the
   * preceding scratch bytes (at [lo..func_start)) shift up. Equivalent
   * to rotating left by (func_start - lo). */
  reverse_range(s_buf, lo, func_start);
  reverse_range(s_buf, func_start, hi);
  reverse_range(s_buf, lo, hi);

  new_start = s_arena_used;
  s_arena_used = (uint16_t)(s_arena_used + func_size);
  return new_start;
}

#else  /* WITH_AUX_COMPILE — windowed buffer */

uint16_t bcbuf_rotate_func_into_arena(uint16_t func_start,
                                      uint16_t func_size) {
  uint16_t lo = s_arena_used;
  uint16_t hi = (uint16_t)(func_start + func_size);
  uint16_t new_start;

  /* Same checks, but the resident-window bound replaces the flat capacity:
   * [lo..hi) is the scratch tail (func body + preceding scratch), always
   * resident (lo == arena_used >= win_base, hi == bc_pos in the window). */
  if (func_start < lo) return (uint16_t)0xFFFF;
  if ((uint16_t)(hi - s_win_base) > (uint16_t)FILE_BC_SIZE) return (uint16_t)0xFFFF;
  if (func_start > hi) return (uint16_t)0xFFFF;

  /* Rotate on window-relative indices (subtract the flushed base). */
  reverse_range(s_buf, (uint16_t)(lo - s_win_base),
                (uint16_t)(func_start - s_win_base));
  reverse_range(s_buf, (uint16_t)(func_start - s_win_base),
                (uint16_t)(hi - s_win_base));
  reverse_range(s_buf, (uint16_t)(lo - s_win_base),
                (uint16_t)(hi - s_win_base));

  new_start = s_arena_used;
  s_arena_used = (uint16_t)(s_arena_used + func_size);
  return new_start;
}

int bcbuf_put(uint16_t abs, unsigned char b) {
  uint16_t off = (uint16_t)(abs - s_win_base);

  if (off >= (uint16_t)FILE_BC_SIZE) {
    /* Window full at the append frontier. Flush the frozen arena prefix
     * [s_win_base..s_arena_used) to aux, slide the resident scratch down to
     * s_buf[0], and re-base. Frozen bytes are immutable and never re-read by
     * the compiler, so this is an append-only flush (no write-back). */
    uint16_t frozen = (uint16_t)(s_arena_used - s_win_base);
    uint16_t resident;
    uint16_t i;
    if (frozen == 0) return 0;   /* scratch alone overflows the window */
    aux_store_write(s_win_base, s_buf, frozen);
    resident = (uint16_t)(abs - s_arena_used);   /* scratch bytes to keep */
    for (i = 0; i < resident; ++i) s_buf[i] = s_buf[(uint16_t)(frozen + i)];
    s_win_base = s_arena_used;
    off = (uint16_t)(abs - s_win_base);
    if (off >= (uint16_t)FILE_BC_SIZE) return 0;  /* still too big */
  }
  s_buf[off] = b;
  return 1;
}

unsigned char bcbuf_get(uint16_t abs) {
  return s_buf[(uint16_t)(abs - s_win_base)];
}

uint16_t bcbuf_flushed(void) {
  return s_win_base;
}

#endif /* WITH_AUX_COMPILE */
