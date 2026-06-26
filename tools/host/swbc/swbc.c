/* swbc — host-side `.swift` -> `.swb` compiler.
 *
 * The on-disk Apple II Compiler caps flat Tier-1 bytecode at
 * FILE_BC_SIZE=1834, so it cannot emit a program large enough to exercise the
 * //e Runner's aux paging
 * beyond the old in-MAIN image cap. This host tool links the same compiler +
 * `.swb` writer with a large FILE_BC_SIZE, so we can build an oversized but
 * valid `.swb`, drop it on a disk, and run it on the paged Runner on a real
 * machine / emulator — the on-target verification the host unit tests can't do
 * (they don't exercise the AUXMOVE asm driver).
 *
 * Usage: swbc IN.swift OUT.swb
 *
 * The produced `.swb` is byte-format-identical to what the on-disk Compiler
 * emits (same opcode set, builtin ids, SWB_VERSION), just potentially larger,
 * so the Runner loads it unchanged.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "common/errors.h"
#include "compiler/compiler.h"
#include "compiler/globals.h"
#include "compiler/srcwin.h"
#include "runtime/heap.h"
#include "swb/swb.h"
#include "platform/platform.h"
#ifdef WITH_AUX_COMPILE
#include "common/aux_store.h"
#include "compiler/bcbuf.h"
static unsigned char s_aux[65536];   /* host stand-in for the aux store */
#endif

extern unsigned char *bcbuf_data(void);

int main(int argc, char **argv) {
  static char src[65536];
  static unsigned char image[32768];
  FILE *fi, *fo;
  size_t n;
  CompileResult cr;
  swb_err_t se;
  uint16_t img_len;

  if (argc < 3 || argc > 4) {
    fprintf(stderr, "usage: %s IN.swift OUT.swb [stream]\n", argv[0]);
    return 2;
  }

  platform_init();
  globals_reset();
  heap_reset();
#ifdef WITH_AUX_COMPILE
  aux_store_host_attach(s_aux);   /* paged build: bcbuf flushes here */
#endif

  /* `stream` mode mirrors the on-disk compiler exactly: feed source through a
   * sliding srcwin window (cap = SWBC_WIN, default 4096 = the //e FILE_SRC_SIZE)
   * instead of one whole buffer — so it exercises window slides + paging
   * together, the combination the on-target failure needs. */
  if (argc == 4 && argv[3][0] == 's') {
#ifndef SWBC_WIN
#define SWBC_WIN 4096
#endif
    static char win[SWBC_WIN];
    SrcWin w;
    long wn = srcwin_open(&w, win, (uint16_t)sizeof win, argv[1]);
    if (wn < 0) { fprintf(stderr, "swbc: srcwin_open failed\n"); return 1; }
    compiler_set_refill(srcwin_refill, &w);
    compiler_compile_source(win, (uint16_t)wn, &cr);
    compiler_set_refill(0, 0);
    srcwin_close(&w);
  } else {
    fi = fopen(argv[1], "rb");
    if (!fi) { perror("swbc: open input"); return 1; }
    n = fread(src, 1, sizeof src - 1, fi);
    fclose(fi);
    src[n] = '\0';
    if (n >= sizeof src - 1) {
      fprintf(stderr, "swbc: input too large (>%zu B)\n", sizeof src - 1);
      return 1;
    }
    compiler_compile_source(src, (uint16_t)n, &cr);
  }
  if (cr.err != SE_OK) {
    fprintf(stderr, "swbc: compile error %d at line %u: %s\n", (int)cr.err,
            cr.err_line, cr.err_msg ? cr.err_msg : "(no message)");
    platform_shutdown();
    return 1;
  }

  se = swb_write(bcbuf_data(), cr.bc_len, cr.program_start,
                 image, (uint16_t)sizeof image, &img_len);
  platform_shutdown();
  if (se != SWB_OK) {
    fprintf(stderr, "swbc: swb_write error %d\n", (int)se);
    return 1;
  }

  fo = fopen(argv[2], "wb");
  if (!fo) { perror("swbc: open output"); return 1; }
  if (fwrite(image, 1, img_len, fo) != img_len) {
    perror("swbc: write output"); fclose(fo); return 1;
  }
  fclose(fo);

  fprintf(stderr, "swbc: %s -> %s  (bytecode %u B, image %u B)\n",
          argv[1], argv[2], cr.bc_len, img_len);
  return 0;
}
