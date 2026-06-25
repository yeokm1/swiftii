/* Paged Runner tests (-DWITH_AUX_BC).
 *
 * Validates the windowed bytecode path: compile a program to a `.swb`, then
 * run it the way the //e Runner does — stream the bytecode into a backing
 * store (here a host buffer standing in for aux), keep only a small MAIN
 * window of it live (bcwin.c), and restore the const-heap + funcs tail in
 * MAIN. This binary is built with a deliberately TINY BC_WINDOW so even
 * small programs repage constantly, stressing every control-flow path:
 * sequential overrun, backward loop jumps, and forward calls/returns.
 *
 * It has its own main() (vm.c here is compiled with WITH_AUX_BC, so it can't
 * share the main unit-test binary's non-paged vm.o). Output for each program
 * must exactly match its known result, proving the window is transparent.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "common/errors.h"
#include "compiler/compiler.h"
#include "compiler/globals.h"
#include "vm/vm.h"
#include "vm/bcwin.h"
#include "common/aux_store.h"
#include "runtime/heap.h"
#include "swb/swb.h"
#include "platform/platform.h"

extern unsigned char *bcbuf_data(void);

/* Host stand-in for aux RAM — the full bytecode is staged here and the VM
 * pages a BC_WINDOW-sized slice at a time out of it. */
static unsigned char s_aux[65536];

/* Compile `src`, build a `.swb`, then run it through the paged path with
 * captured stdout. `*bc_len_out` (optional) reports the bytecode size so a
 * test can assert it exceeds the old in-MAIN image cap. Returns 0 on success
 * or a negative step code. */
static int run_paged(const char *src, char *out, size_t out_cap,
                     uint16_t *bc_len_out) {
  CompileResult cr;
  swb_err_t se;
  swiftii_err_t rc;
  static unsigned char image[16384];
  uint16_t img_len, prog_start, bc_len, heap_len;
  uint8_t funcs_n;
  int saved_fd, capture_fd;
  ssize_t n;
  const char *tmp_path = "/tmp/swiftii_paged_test.txt";

  globals_reset();
  vm_reset_globals();
  heap_reset();

  compiler_compile_source(src, (uint16_t)strlen(src), &cr);
  if (cr.err != SE_OK) return -1;

  se = swb_write(bcbuf_data(), cr.bc_len, cr.program_start,
                 image, (uint16_t)sizeof image, &img_len);
  if (se != SWB_OK) return -10 - (int)se;

  /* The Runner path: header -> stage bytecode to (aux) -> restore tail. */
  se = swb_header_info(image, &prog_start, &bc_len, &heap_len, &funcs_n);
  if (se != SWB_OK) return -20 - (int)se;
  if (bc_len_out) *bc_len_out = bc_len;

  aux_store_host_attach(s_aux);
  bcwin_stage(0, image + SWB_HEADER_SIZE, bc_len);
  se = swb_load_tail(image + SWB_HEADER_SIZE + bc_len, heap_len, funcs_n,
                     bc_len);
  if (se != SWB_OK) return -30 - (int)se;

  vm_reset_globals();

  fflush(stdout);
  saved_fd = dup(1);
  if (saved_fd < 0) return -2;
  capture_fd = open(tmp_path, O_RDWR | O_CREAT | O_TRUNC, 0600);
  if (capture_fd < 0) { close(saved_fd); return -3; }
  if (dup2(capture_fd, 1) < 0) { close(saved_fd); close(capture_fd); return -4; }
  close(capture_fd);

  platform_init();
  rc = vm_run((const unsigned char *)0, prog_start, bc_len);
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

/* ---- cases ---- */

static int g_failed;

static void check(const char *name, const char *src, const char *expect) {
  char buf[8192];
  int rc = run_paged(src, buf, sizeof buf, NULL);
  if (rc != 0) {
    printf("FAIL paged::%s (run rc=%d)\n", name, rc);
    g_failed++;
    return;
  }
  if (strcmp(buf, expect) != 0) {
    printf("FAIL paged::%s\n  expected %s\n  got      %s\n", name, expect, buf);
    g_failed++;
    return;
  }
  printf("ok   paged::%s\n", name);
}

/* A program big enough that its bytecode exceeds the old in-MAIN image cap
 * (SWB_IMAGE_SIZE was 2,944), proving the paged path lifts the ceiling. */
static void check_large(void) {
  static char src[16384];
  static char expect[4096];
  char got[4096];
  uint16_t bc_len = 0;
  int i, rc;
  size_t sp = 0, ep = 0;

  for (i = 0; i < 600; ++i) {
    memcpy(src + sp, "print(7)\n", 9); sp += 9;
    memcpy(expect + ep, "7\n", 2); ep += 2;
  }
  src[sp] = '\0';
  expect[ep] = '\0';

  rc = run_paged(src, got, sizeof got, &bc_len);
  if (rc != 0) { printf("FAIL paged::large (run rc=%d)\n", rc); g_failed++; return; }
  if (bc_len <= 2944) {
    printf("FAIL paged::large (bc_len %u did not exceed old cap)\n", bc_len);
    g_failed++; return;
  }
  if (strcmp(got, expect) != 0) {
    printf("FAIL paged::large (output mismatch, bc_len=%u)\n", bc_len);
    g_failed++; return;
  }
  printf("ok   paged::large (bc_len=%u > 2944)\n", bc_len);
}

/* Mirrors the emulator's BIGPROG.SWB (Makefile `bigswb`): `var s = 0` then
 * N x `s = s + 7`, then print(s). Straight-line so the bytecode is large; the
 * expected output (7*N) is what `make run-iz-bigswb-iie` must print on target,
 * so this asserts the same value the on-target run will. */
static void check_accumulate(void) {
  static char src[16384];
  char got[64];
  char expect[16];
  uint16_t bc_len = 0;
  int i, rc;
  const int N = 700;
  size_t sp = 0;

  memcpy(src + sp, "var s = 0\n", 10); sp += 10;
  for (i = 0; i < N; ++i) { memcpy(src + sp, "s = s + 7\n", 10); sp += 10; }
  memcpy(src + sp, "print(s)\n", 9); sp += 9;
  src[sp] = '\0';
  snprintf(expect, sizeof expect, "%d\n", N * 7);

  rc = run_paged(src, got, sizeof got, &bc_len);
  if (rc != 0) { printf("FAIL paged::accumulate (run rc=%d)\n", rc); g_failed++; return; }
  if (bc_len <= 2944) {
    printf("FAIL paged::accumulate (bc_len %u did not exceed old cap)\n", bc_len);
    g_failed++; return;
  }
  if (strcmp(got, expect) != 0) {
    printf("FAIL paged::accumulate (got %s want %s)\n", got, expect);
    g_failed++; return;
  }
  printf("ok   paged::accumulate (bc_len=%u, output=%d)\n", bc_len, N * 7);
}

int main(void) {
  g_failed = 0;

  /* Sequential + simple expression (repages on window overrun). */
  check("arith", "print(21 * 2)\n", "42\n");
  /* Const-pool string travels in the tail, reproduced in MAIN heap. */
  check("string", "print(\"hello, paged world\")\n", "hello, paged world\n");
  check("two_strings", "print(\"ab\")\nprint(\"cd\")\n", "ab\ncd\n");
  /* Backward jumps: a while loop repages every iteration with a tiny window. */
  check("while_loop",
        "var i = 0\nwhile i < 5 { print(i)\ni = i + 1 }\n",
        "0\n1\n2\n3\n4\n");
  /* Forward jump to the function arena + OP_RETURN back. */
  check("func_call",
        "func sq(n: Int) -> Int { return n * n }\nprint(sq(n: 7))\n",
        "49\n");
  /* Recursion: repeated far jumps in and out of the arena (depth kept
   * within VM_CALL_FRAMES=4 — this is a paging test, not a stack test). */
  check("recursion",
        "func sumto(n: Int) -> Int {\n"
        "  if n <= 0 { return 0 }\n"
        "  return n + sumto(n: n - 1)\n"
        "}\n"
        "print(sumto(n: 2))\n",
        "3\n");
  /* for-in over an array + a call inside the loop body. */
  check("for_in_call",
        "func dbl(n: Int) -> Int { return n + n }\n"
        "var xs = [1, 2, 3]\n"
        "var total = 0\n"
        "for i in 0..<xs.count { total = total + dbl(n: xs[i]) }\n"
        "print(total)\n",
        "12\n");

  check_large();
  check_accumulate();

  printf("--- paged runner: %s\n", g_failed == 0 ? "all ok" : "FAILURES");
  return g_failed == 0 ? 0 : 1;
}
