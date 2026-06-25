/* `.swb` serialise (write side). See swb.h for the format.
 *
 * The deserialise half lives in swb_read.c (split 2026-06-12: ld65 links
 * whole objects, so a single TU forced each Family B binary to carry the
 * other's half as dead code). The Compiler links this file only; the
 * Runner links swb_read.c only; host tests link both. No I/O policy
 * here — swb_write takes a caller buffer, and swb_write_stream lets the
 * Compiler stream straight to disk (MLI WRITE) with no full-image buffer.
 * Touches only the heap + funcs singletons.
 */
#include "swb.h"

#include "../common/config.h"
#include "../runtime/heap.h"
#include "../compiler/funcs.h"
#ifdef WITH_AUX_COMPILE
/* Paged Compiler: the bytecode is split — [0..bcbuf_flushed()) lives in the
 * aux store, [bcbuf_flushed()..bc_len) is resident in `bc` (= bcbuf_data()). */
#include "../compiler/bcbuf.h"
#include "../common/aux_store.h"
#endif

static void put_u16(unsigned char *p, uint16_t v) {
  p[0] = (unsigned char)(v & 0xFF);
  p[1] = (unsigned char)((v >> 8) & 0xFF);
}

static void write_header(unsigned char *out, uint16_t bc_len,
                         uint16_t program_start, uint16_t heap_len,
                         uint8_t funcs_n) {
  out[0] = 'S';
  out[1] = 'W';
  out[2] = 'B';
  out[3] = (unsigned char)SWB_VERSION;
  put_u16(&out[4], program_start);
  put_u16(&out[6], bc_len);
  put_u16(&out[8], heap_len);
  out[10] = funcs_n;
  out[11] = 0;
}

swb_err_t swb_write(const unsigned char *bc, uint16_t bc_len,
                    uint16_t program_start,
                    unsigned char *out, uint16_t out_cap,
                    uint16_t *out_len) {
  const unsigned char *heap_src;
  uint16_t heap_len;
  uint8_t funcs_n;
  uint32_t total;
  uint16_t pos;
  uint16_t i;

  heap_len = heap_const_image(&heap_src);
  funcs_n = funcs_count();

  total = (uint32_t)SWB_HEADER_SIZE + bc_len + heap_len
          + (uint32_t)funcs_n * SWB_FUNC_SIZE;
  if (total > (uint32_t)out_cap) return SWB_ERR_OUT_FULL;

  write_header(out, bc_len, program_start, heap_len, funcs_n);
  pos = SWB_HEADER_SIZE;

#ifdef WITH_AUX_COMPILE
  {
    uint16_t flushed = bcbuf_flushed();
    uint16_t k;
    if (flushed) { aux_store_read(0, &out[pos], flushed); pos += flushed; }
    for (k = flushed; k < bc_len; ++k) out[pos++] = bc[(uint16_t)(k - flushed)];
  }
#else
  for (i = 0; i < bc_len; ++i) out[pos++] = bc[i];
#endif
  for (i = 0; i < heap_len; ++i) out[pos++] = heap_src[i];
  for (i = 0; i < funcs_n; ++i) {
    put_u16(&out[pos], funcs_get_start((uint8_t)i));
    pos += 2;
    out[pos++] = funcs_get_param_count((uint8_t)i);
    out[pos++] = funcs_has_return((uint8_t)i);
  }

  *out_len = pos;
  return SWB_OK;
}

swb_err_t swb_write_stream(const unsigned char *bc, uint16_t bc_len,
                           uint16_t program_start,
                           swb_writer wr, void *ctx) {
  unsigned char hdr[SWB_HEADER_SIZE];
  unsigned char fbuf[SWB_FUNC_SIZE];
  const unsigned char *heap_src;
  uint16_t heap_len;
  uint8_t funcs_n;
  uint8_t i;

  heap_len = heap_const_image(&heap_src);
  funcs_n = funcs_count();

  write_header(hdr, bc_len, program_start, heap_len, funcs_n);
  if (wr(ctx, hdr, SWB_HEADER_SIZE) != 0) return SWB_ERR_IO;
#ifdef WITH_AUX_COMPILE
  {
    uint16_t flushed = bcbuf_flushed();
    uint16_t off = 0;
    unsigned char tmp[128];
    while (off < flushed) {
      uint16_t n = (uint16_t)(flushed - off);
      if (n > (uint16_t)sizeof tmp) n = (uint16_t)sizeof tmp;
      aux_store_read(off, tmp, n);
      if (wr(ctx, tmp, n) != 0) return SWB_ERR_IO;
      off = (uint16_t)(off + n);
    }
    if (bc_len > flushed &&
        wr(ctx, bc, (uint16_t)(bc_len - flushed)) != 0) return SWB_ERR_IO;
  }
#else
  if (bc_len && wr(ctx, bc, bc_len) != 0) return SWB_ERR_IO;
#endif
  if (heap_len && wr(ctx, heap_src, heap_len) != 0) return SWB_ERR_IO;
  for (i = 0; i < funcs_n; ++i) {
    put_u16(&fbuf[0], funcs_get_start(i));
    fbuf[2] = funcs_get_param_count(i);
    fbuf[3] = funcs_has_return(i);
    if (wr(ctx, fbuf, SWB_FUNC_SIZE) != 0) return SWB_ERR_IO;
  }
  return SWB_OK;
}
