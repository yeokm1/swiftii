/* `.swb` deserialise (read side). See swb.h for the format.
 *
 * Split from swb.c (2026-06-12): ld65 links whole objects, so a single TU
 * forced the Compiler to carry the Runner's read side as dead code and
 * vice versa — and the Compiler's BSS margin (~50 B) couldn't absorb the
 * bounds-validation bytes. The Compiler links swb.c (write side) only;
 * the Runner links this file only; host tests link both.
 */
#include "swb.h"

#include "../common/config.h"
#include "../runtime/heap.h"
#include "../compiler/funcs.h"

static uint16_t get_u16(const unsigned char *p) {
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

/* Parsed header fields + the offset where each section begins. */
typedef struct {
  uint16_t program_start;
  uint16_t bc_len;
  uint16_t heap_len;
  uint8_t  funcs_n;
  uint16_t heap_off;   /* in_len-relative offset of the heap-const section */
  uint16_t funcs_off;  /* ... of the funcs section */
} swb_hdr_t;

/* Validate magic/version and that `in_len` actually covers every declared
 * section; fill `*h`. Pure (no side effects) so callers can bail cleanly. */
static swb_err_t parse_header(const unsigned char *in, uint16_t in_len,
                              swb_hdr_t *h) {
  uint32_t total;

  if (in_len < SWB_HEADER_SIZE) return SWB_ERR_TRUNC;
  if (in[0] != 'S' || in[1] != 'W' || in[2] != 'B') return SWB_ERR_MAGIC;
  if (in[3] != (unsigned char)SWB_VERSION) return SWB_ERR_MAGIC;

  h->program_start = get_u16(&in[4]);
  h->bc_len = get_u16(&in[6]);
  h->heap_len = get_u16(&in[8]);
  h->funcs_n = in[10];
  h->heap_off = (uint16_t)(SWB_HEADER_SIZE + h->bc_len);
  h->funcs_off = (uint16_t)(h->heap_off + h->heap_len);

  total = (uint32_t)SWB_HEADER_SIZE + h->bc_len + h->heap_len
          + (uint32_t)h->funcs_n * SWB_FUNC_SIZE;
  if (total > (uint32_t)in_len) return SWB_ERR_TRUNC;

  /* Entry PC must lie inside the bytecode (== bc_len is legal: an empty
   * top-level after the function arena halts immediately). A corrupt image
   * would otherwise "run" as a silent no-op instead of erroring. */
  if (h->program_start > h->bc_len) return SWB_ERR_BOUNDS;
  return SWB_OK;
}

/* Rebuild the heap + funcs singletons from a validated image. */
static swb_err_t load_state(const unsigned char *in, const swb_hdr_t *h) {
  uint16_t pos;
  uint8_t i;

  if ((uint32_t)STRING_POOL_SLOTS + h->heap_len > (uint32_t)HEAP_SIZE)
    return SWB_ERR_HEAP_CAP;
  if (h->funcs_n > (uint8_t)MAX_FUNCS) return SWB_ERR_FUNCS_CAP;

  /* Every func entry's start PC must point at an instruction inside the
   * bytecode. Pre-validate before touching the singletons so a bad image
   * leaves no half-loaded state. */
  pos = h->funcs_off;
  for (i = 0; i < h->funcs_n; ++i) {
    if (get_u16(&in[pos]) >= h->bc_len) return SWB_ERR_BOUNDS;
    pos += SWB_FUNC_SIZE;
  }

  heap_reset();
  heap_load_const(&in[h->heap_off], h->heap_len);

  funcs_reset();
  pos = h->funcs_off;
  for (i = 0; i < h->funcs_n; ++i) {
    funcs_add_runtime(get_u16(&in[pos]), in[pos + 2], in[pos + 3]);
    pos += SWB_FUNC_SIZE;
  }
  return SWB_OK;
}

swb_err_t swb_read(const unsigned char *in, uint16_t in_len,
                   unsigned char *bc_out, uint16_t bc_cap,
                   uint16_t *program_start, uint16_t *bc_len) {
  swb_hdr_t h;
  swb_err_t e;
  uint16_t i;

  e = parse_header(in, in_len, &h);
  if (e != SWB_OK) return e;
  if (h.bc_len > bc_cap) return SWB_ERR_BC_CAP;  /* before any side effects */

  e = load_state(in, &h);
  if (e != SWB_OK) return e;

  for (i = 0; i < h.bc_len; ++i) bc_out[i] = in[SWB_HEADER_SIZE + i];
  *program_start = h.program_start;
  *bc_len = h.bc_len;
  return SWB_OK;
}

swb_err_t swb_open_image(const unsigned char *in, uint16_t in_len,
                         const unsigned char **bc,
                         uint16_t *program_start, uint16_t *bc_len) {
  swb_hdr_t h;
  swb_err_t e;

  e = parse_header(in, in_len, &h);
  if (e != SWB_OK) return e;

  e = load_state(in, &h);
  if (e != SWB_OK) return e;

  *bc = &in[SWB_HEADER_SIZE];  /* bytecode runs in place inside the image */
  *program_start = h.program_start;
  *bc_len = h.bc_len;
  return SWB_OK;
}

#if defined(WITH_AUX_BC) || !defined(__CC65__)
/* Paged loading (Family B //e Runner): the bytecode is streamed into aux,
 * never held whole in MAIN, so the two whole-image entry points above can't
 * be used. The Runner instead (1) reads the 12-byte header and calls
 * swb_header_info to learn the section sizes, (2) streams `bc_len` bytecode
 * bytes from the file straight to aux, then (3) reads the small const-heap +
 * funcs tail into a MAIN buffer and calls swb_load_tail to restore them.
 * Compiled in for the host (tests) and the WITH_AUX_BC //e Runner only. */
swb_err_t swb_header_info(const unsigned char *in,
                          uint16_t *program_start, uint16_t *bc_len,
                          uint16_t *heap_len, uint8_t *funcs_n) {
  uint16_t ps, bc;
  if (in[0] != 'S' || in[1] != 'W' || in[2] != 'B') return SWB_ERR_MAGIC;
  if (in[3] != (unsigned char)SWB_VERSION) return SWB_ERR_MAGIC;
  ps = get_u16(&in[4]);
  bc = get_u16(&in[6]);
  if (ps > bc) return SWB_ERR_BOUNDS;   /* entry PC inside the bytecode */
  *program_start = ps;
  *bc_len = bc;
  *heap_len = get_u16(&in[8]);
  *funcs_n = in[10];
  return SWB_OK;
}

/* Rebuild heap + funcs from the on-disk tail (const-heap then funcs records),
 * the bytecode having already been staged to aux. Mirrors load_state but
 * indexes the tail from 0 instead of the bytecode-relative offsets. */
swb_err_t swb_load_tail(const unsigned char *tail, uint16_t heap_len,
                        uint8_t funcs_n, uint16_t bc_len) {
  const unsigned char *funcs_src = tail + heap_len;
  uint16_t pos;
  uint8_t i;

  if ((uint32_t)STRING_POOL_SLOTS + heap_len > (uint32_t)HEAP_SIZE)
    return SWB_ERR_HEAP_CAP;
  if (funcs_n > (uint8_t)MAX_FUNCS) return SWB_ERR_FUNCS_CAP;

  for (i = 0, pos = 0; i < funcs_n; ++i, pos += SWB_FUNC_SIZE) {
    if (get_u16(&funcs_src[pos]) >= bc_len) return SWB_ERR_BOUNDS;
  }

  heap_reset();
  heap_load_const(tail, heap_len);

  funcs_reset();
  for (i = 0, pos = 0; i < funcs_n; ++i, pos += SWB_FUNC_SIZE) {
    funcs_add_runtime(get_u16(&funcs_src[pos]), funcs_src[pos + 2],
                      funcs_src[pos + 3]);
  }
  return SWB_OK;
}
#endif
