/* Paged Compiler tests (-DWITH_AUX_COMPILE).
 *
 * Validates the Compiler's append-flush bytecode paging: completed function
 * bodies are immutable and never re-read, so as the arena grows past the MAIN
 * window the frozen prefix is flushed to the aux store; only the in-progress
 * function + the still-mutable top-level scratch stay resident. This binary is
 * built with a TINY FILE_BC_SIZE (the window) so a function-heavy program
 * forces real flushes, and WITHOUT WITH_AUX_BC so the VM + swb_read side stay
 * flat — letting us compile (paged) -> .swb -> read back -> run -> check the
 * output end-to-end in one process.
 *
 * Own main(): bcbuf.c/swb.c/emit.c here are the paged variants, incompatible
 * with the main unit-test binary's flat ones.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "common/errors.h"
#include "common/aux_store.h"
#include "compiler/compiler.h"
#include "compiler/globals.h"
#include "compiler/funcs.h"
#include "compiler/bcbuf.h"
#include "compiler/srcwin.h"
#include "vm/vm.h"
#include "runtime/heap.h"
#include "swb/swb.h"
#include "platform/platform.h"

extern unsigned char *bcbuf_data(void);

/* Host stand-in for the aux bytecode store (flushed function bodies). */
static unsigned char s_aux[65536];

static int g_failed;

/* Compile `src` with the paged buffer, write the .swb (assembled from aux +
 * the resident window), read it back into a flat buffer, run it, and capture
 * stdout. Reports how many bytes were flushed (proof the arena offloaded).
 * Returns 0 on success or a negative step code. */
static int compile_paged_run(const char *src, char *out, size_t out_cap,
                             uint16_t *flushed_out, uint16_t *bc_len_out) {
  CompileResult cr;
  swb_err_t se;
  swiftii_err_t rc;
  static unsigned char image[32768];
  static unsigned char runner_bc[32768];
  uint16_t img_len, ps, bl;
  int saved_fd, capture_fd;
  ssize_t n;
  const char *tmp_path = "/tmp/swiftii_compiler_paged_test.txt";

  globals_reset();
  funcs_reset();        /* cascades bcbuf_arena_reset -> resets the window base */
  heap_reset();
  vm_reset_globals();

  aux_store_host_attach(s_aux);

  compiler_compile_source(src, (uint16_t)strlen(src), &cr);
  if (cr.err != SE_OK) return -1;

  se = swb_write(bcbuf_data(), cr.bc_len, cr.program_start,
                 image, (uint16_t)sizeof image, &img_len);
  if (se != SWB_OK) return -10 - (int)se;
  if (flushed_out) *flushed_out = bcbuf_flushed();
  if (bc_len_out) *bc_len_out = cr.bc_len;

  /* Read the paged-produced image back the normal (flat) way and run it. */
  se = swb_read(image, img_len, runner_bc, (uint16_t)sizeof runner_bc, &ps, &bl);
  if (se != SWB_OK) return -20 - (int)se;

  vm_reset_globals();

  fflush(stdout);
  saved_fd = dup(1);
  if (saved_fd < 0) return -2;
  capture_fd = open(tmp_path, O_RDWR | O_CREAT | O_TRUNC, 0600);
  if (capture_fd < 0) { close(saved_fd); return -3; }
  if (dup2(capture_fd, 1) < 0) { close(saved_fd); close(capture_fd); return -4; }
  close(capture_fd);

  platform_init();
  rc = vm_run(runner_bc, ps, bl);
  platform_shutdown();
  fflush(stdout);

  dup2(saved_fd, 1);
  close(saved_fd);

  if (rc != SE_OK) return -5;

  capture_fd = open(tmp_path, O_RDONLY);
  if (capture_fd < 0) return -6;
  n = read(capture_fd, out, out_cap - 1);
  close(capture_fd);
  if (n < 0) return -7;
  out[n] = '\0';
  return 0;
}

static void check(const char *name, const char *src, const char *expect) {
  char buf[256];
  int rc = compile_paged_run(src, buf, sizeof buf, NULL, NULL);
  if (rc != 0) { printf("FAIL compiler-paged::%s (rc=%d)\n", name, rc); g_failed++; return; }
  if (strcmp(buf, expect) != 0) {
    printf("FAIL compiler-paged::%s (got %s want %s)\n", name, buf, expect);
    g_failed++; return;
  }
  printf("ok   compiler-paged::%s\n", name);
}

/* Many functions (a large arena that must flush to aux past the tiny window),
 * a small top-level that calls a few of them. Each fK(n:) returns n + K, so
 * calling with n=0 yields K; summing K in {0,6,12,18} -> 36. Runtime call
 * depth is 1 (no nesting), so VM_CALL_FRAMES is not the limit here. Asserts a
 * flush actually happened (frozen functions left MAIN). */
static void check_function_heavy(void) {
  static char src[16384];
  char got[64];
  uint16_t flushed = 0, bc_len = 0;
  int i, rc;
  size_t sp = 0;
  char line[64];
  const int N = 45;   /* enough functions that the arena exceeds the window */

  for (i = 0; i < N; ++i) {
    int m = snprintf(line, sizeof line,
                     "func f%d(n: Int) -> Int { return n + %d }\n", i, i);
    memcpy(src + sp, line, (size_t)m); sp += (size_t)m;
  }
  sp += (size_t)snprintf(src + sp, sizeof src - sp, "var s = 0\n");
  sp += (size_t)snprintf(src + sp, sizeof src - sp, "s = s + f0(0)\n");
  sp += (size_t)snprintf(src + sp, sizeof src - sp, "s = s + f6(0)\n");
  sp += (size_t)snprintf(src + sp, sizeof src - sp, "s = s + f12(0)\n");
  sp += (size_t)snprintf(src + sp, sizeof src - sp, "s = s + f18(0)\n");
  sp += (size_t)snprintf(src + sp, sizeof src - sp, "print(s)\n");
  src[sp] = '\0';

  rc = compile_paged_run(src, got, sizeof got, &flushed, &bc_len);
  if (rc != 0) { printf("FAIL compiler-paged::func_heavy (rc=%d)\n", rc); g_failed++; return; }
  if (flushed == 0) {
    printf("FAIL compiler-paged::func_heavy (nothing flushed; window too big to test paging, bc_len=%u)\n", bc_len);
    g_failed++; return;
  }
  if (strcmp(got, "36\n") != 0) {
    printf("FAIL compiler-paged::func_heavy (got %s want 36, bc_len=%u flushed=%u)\n", got, bc_len, flushed);
    g_failed++; return;
  }
  printf("ok   compiler-paged::func_heavy (bc_len=%u, flushed=%u to aux)\n", bc_len, flushed);
}

/* Validates the OUTPUT of the shipped datadisk/xsamples/xfuncs.swift showcase:
 * 20 functions summed f1..f20 = 210 (the value `make run-iz-...-iie` should
 * print on target). The shipped sample bulks each body with filler so total
 * bytecode exceeds the flat 1,834 B cap (Tier 2/3 can compile it); the
 * filler doesn't change the result, and would overflow this binary's tiny
 * window, so here the bodies are minimal — output + flush behaviour are what
 * matter. (The shipped big-body version's paged compile is checked via
 * `make swbc-aux` + the paged==flat .swb comparison.) */
static void check_xfuncs(void) {
  static char src[16384];
  char got[64];
  uint16_t flushed = 0, bc_len = 0;
  int k, rc;
  size_t sp = 0;
  char line[64];

  for (k = 1; k <= 20; ++k) {
    int m = snprintf(line, sizeof line,
                     "func f%d(n: Int) -> Int { return %d }\n", k, k);
    memcpy(src + sp, line, (size_t)m); sp += (size_t)m;
  }
  sp += (size_t)snprintf(src + sp, sizeof src - sp, "var s = 0\n");
  for (k = 1; k <= 20; ++k) {
    int m = snprintf(line, sizeof line, "s = s + f%d(0)\n", k);
    memcpy(src + sp, line, (size_t)m); sp += (size_t)m;
  }
  sp += (size_t)snprintf(src + sp, sizeof src - sp, "print(s)\n");
  src[sp] = '\0';

  rc = compile_paged_run(src, got, sizeof got, &flushed, &bc_len);
  if (rc != 0) { printf("FAIL compiler-paged::xfuncs (rc=%d)\n", rc); g_failed++; return; }
  if (flushed == 0 || strcmp(got, "210\n") != 0) {
    printf("FAIL compiler-paged::xfuncs (got %s want 210, bc_len=%u flushed=%u)\n", got, bc_len, flushed);
    g_failed++; return;
  }
  printf("ok   compiler-paged::xfuncs (bc_len=%u, flushed=%u, output=210)\n", bc_len, flushed);
}

/* The on-target reproduction: compile the SHIPPED xfuncs (full filler bodies)
 * the way the on-disk //e Compiler does — STREAMING the source through srcwin
 * (small window) while the paged bcbuf flushes to aux mid-compile. Both host
 * tests above compile the whole source in one call, so they miss any
 * streaming x flush interaction. Expect 210. */
static void check_xfuncs_streamed(void) {
  static char src[16384];
  static char win[4096];   /* match the //e Compiler's FILE_SRC_SIZE so the
                              first source slide lands where it does on target */
  char got[64];
  int k, j;
  size_t sp = 0;
  const char *path = "/tmp/swiftii_xfuncs_stream.swift";
  FILE *f;
  SrcWin w;
  CompileResult cr;
  long n;

  /* Full filler bodies (like the shipped sample) so flushes are frequent. */
  for (k = 1; k <= 20; ++k) {
    sp += (size_t)snprintf(src + sp, sizeof src - sp,
                           "func f%d(n: Int) -> Int { var a = n\n", k);
    for (j = 0; j < 12; ++j) { memcpy(src + sp, "a = a + 1\n", 10); sp += 10; }
    sp += (size_t)snprintf(src + sp, sizeof src - sp, "return %d }\n", k);
  }
  sp += (size_t)snprintf(src + sp, sizeof src - sp, "var s = 0\n");
  for (k = 1; k <= 20; ++k)
    sp += (size_t)snprintf(src + sp, sizeof src - sp, "s = s + f%d(0)\n", k);
  sp += (size_t)snprintf(src + sp, sizeof src - sp, "print(s)\n");

  f = fopen(path, "wb");
  if (!f || fwrite(src, 1, sp, f) != sp) { printf("FAIL compiler-paged::xfuncs_stream (write)\n"); g_failed++; if (f) fclose(f); return; }
  fclose(f);

  globals_reset(); funcs_reset(); heap_reset(); vm_reset_globals();
  aux_store_host_attach(s_aux);

  n = srcwin_open(&w, win, (uint16_t)sizeof win, path);
  if (n < 0) { printf("FAIL compiler-paged::xfuncs_stream (open)\n"); g_failed++; return; }
  compiler_set_refill(srcwin_refill, &w);
  compiler_compile_source(win, (uint16_t)n, &cr);
  compiler_set_refill(0, 0);
  srcwin_close(&w);

  if (cr.err != SE_OK) {
    printf("FAIL compiler-paged::xfuncs_stream (compile err %d line %u: %s)\n",
           (int)cr.err, cr.err_line, cr.err_msg ? cr.err_msg : "?");
    g_failed++; return;
  }
  /* Reuse the compile -> .swb -> read -> run path via a second whole pass is
   * overkill; just check the bytecode runs (heap/funcs already rebuilt by the
   * compile). Run straight from bcbuf is not possible (paged), so write+read. */
  {
    static unsigned char image[32768], runner_bc[32768];
    swb_err_t se; uint16_t img_len, ps, bl; swiftii_err_t vrc;
    int sfd, cfd; ssize_t rd; const char *cap = "/tmp/swiftii_xfstream.txt";
    se = swb_write(bcbuf_data(), cr.bc_len, cr.program_start, image, (uint16_t)sizeof image, &img_len);
    if (se != SWB_OK) { printf("FAIL xfuncs_stream (swb_write %d)\n", (int)se); g_failed++; return; }
    se = swb_read(image, img_len, runner_bc, (uint16_t)sizeof runner_bc, &ps, &bl);
    if (se != SWB_OK) { printf("FAIL xfuncs_stream (swb_read %d)\n", (int)se); g_failed++; return; }
    vm_reset_globals();
    fflush(stdout); sfd = dup(1); cfd = open(cap, O_RDWR|O_CREAT|O_TRUNC, 0600);
    dup2(cfd, 1); close(cfd); platform_init(); vrc = vm_run(runner_bc, ps, bl);
    platform_shutdown(); fflush(stdout); dup2(sfd, 1); close(sfd);
    cfd = open(cap, O_RDONLY); rd = read(cfd, got, sizeof got - 1); close(cfd);
    got[rd < 0 ? 0 : rd] = '\0';
    if (vrc != SE_OK || strcmp(got, "210\n") != 0) {
      printf("FAIL compiler-paged::xfuncs_stream (got %s want 210)\n", got); g_failed++; return;
    }
  }
  printf("ok   compiler-paged::xfuncs_stream (streamed + paged, output=210)\n");
}

int main(void) {
  g_failed = 0;

  /* Small programs that fit the window (no flush): basic correctness. */
  check("arith", "print(21 * 2)\n", "42\n");
  check("one_func",
        "func sq(n: Int) -> Int { return n * n }\nprint(sq(n: 7))\n",
        "49\n");

  /* The real test: a large arena that flushes to aux. */
  check_function_heavy();
  check_xfuncs();
  check_xfuncs_streamed();

  printf("--- compiler paged: %s\n", g_failed == 0 ? "all ok" : "FAILURES");
  return g_failed == 0 ? 0 : 1;
}
