/* Streaming source window tests (Tier 2, doc 016).
 *
 * The host stand-in for the Family B Compiler's disk streaming: write a
 * source file much larger than the window, compile it through srcwin with
 * deliberately tiny windows (forcing many statement-boundary slides), run
 * the bytecode, and diff the output against a whole-buffer compile of the
 * same file. Also locks in the failure mode: a single statement longer
 * than the window must fail the compile with `eof` still 0 (that flag is
 * how compiler_main distinguishes "statement too long" from a plain
 * syntax error).
 *
 * Invariant under test (srcwin_refill): the slide triggers at a separator
 * once the scan point passes cap/2, so any single statement up to cap/2
 * is guaranteed to fit; the repeated blocks below keep lines well under
 * the smallest test window's half.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "common/errors.h"
#include "compiler/compiler.h"
#include "compiler/globals.h"
#include "compiler/srcwin.h"
#include "vm/vm.h"
#include "runtime/heap.h"
#include "platform/platform.h"

#define EXPECT(cond, code) do { if (!(cond)) return (code); } while (0)

extern unsigned char *bcbuf_data(void);

static const char *k_src_path = "/tmp/swiftii_srcwin_test.swift";
static const char *k_cap_path = "/tmp/swiftii_srcwin_test.txt";

/* Write a program of `blocks` repeated statement groups. Each block is
 * mostly comment bytes (high source-to-bytecode ratio, so the source can
 * dwarf the window while the bytecode stays inside the host's 1 KB
 * arena) plus statements that exercise calls, interpolation re-lexing,
 * and control flow across window slides. */
static long write_big_source(int blocks) {
  long total = 0;
  int b, n;
  FILE *f = fopen(k_src_path, "wb");
  if (!f) return -1;
  n = fprintf(f,
              "var total = 0\n"
              "var msg = \"go\"\n"
              "func bump(x: Int) -> Int { return x + 3 }\n");
  if (n < 0) { fclose(f); return -1; }
  total += n;
  for (b = 0; b < blocks; ++b) {
    n = fprintf(f,
                "// pad pad pad pad pad pad pad pad pad pad pad %04d\n"
                "total = bump(total)\n"
                "msg = \"t=\\(total)\"\n"
                "if total > 10 { total = total - 1 }\n",
                b);
    if (n < 0) { fclose(f); return -1; }
    total += n;
  }
  n = fprintf(f, "print(msg)\nprint(total)\n");
  if (n < 0) { fclose(f); return -1; }
  total += n;
  fclose(f);
  return total;
}

/* Run bcbuf's compiled program with stdout captured into `out`. */
static int run_capture(const CompileResult *cr, char *out, size_t out_cap) {
  swiftii_err_t rc;
  int saved_fd, capture_fd;
  ssize_t n;

  fflush(stdout);
  saved_fd = dup(1);
  if (saved_fd < 0) return -2;
  capture_fd = open(k_cap_path, O_RDWR | O_CREAT | O_TRUNC, 0600);
  if (capture_fd < 0) { close(saved_fd); return -3; }
  if (dup2(capture_fd, 1) < 0) { close(saved_fd); close(capture_fd); return -4; }
  close(capture_fd);

  platform_init();
  rc = vm_run(bcbuf_data(), cr->program_start, cr->bc_len);
  platform_shutdown();
  fflush(stdout);

  dup2(saved_fd, 1);
  close(saved_fd);

  if (rc != SE_OK) return -5;

  capture_fd = open(k_cap_path, O_RDONLY);
  if (capture_fd < 0) return -6;
  n = read(capture_fd, out, out_cap - 1);
  close(capture_fd);
  if (n < 0) return -7;
  out[n] = '\0';
  return 0;
}

/* Whole-buffer reference: read the file into one big buffer and compile
 * it the pre-Tier-2 way. */
static int whole_run(char *out, size_t out_cap) {
  static char big[8192];
  CompileResult cr;
  size_t len;
  FILE *f = fopen(k_src_path, "rb");
  if (!f) return -10;
  len = fread(big, 1, sizeof big - 1, f);
  fclose(f);

  globals_reset();
  vm_reset_globals();
  heap_reset();
  compiler_compile_source(big, (uint16_t)len, &cr);
  if (cr.err != SE_OK) return -11;
  return run_capture(&cr, out, out_cap);
}

/* Streaming compile through a `wincap`-byte window. Returns 0 on success,
 * -12 on open failure, -13 on compile error (then sets *eof_out so the
 * caller can check the statement-too-long flag). */
static int stream_run(uint16_t wincap, char *out, size_t out_cap,
                      unsigned char *eof_out) {
  static char win[1024];
  SrcWin w;
  CompileResult cr;
  long n;

  globals_reset();
  vm_reset_globals();
  heap_reset();

  n = srcwin_open(&w, win, wincap, k_src_path);
  if (n < 0) return -12;
  compiler_set_refill(srcwin_refill, &w);
  compiler_compile_source(win, (uint16_t)n, &cr);
  compiler_set_refill(0, 0);
  srcwin_close(&w);
  if (eof_out) *eof_out = w.eof;
  if (cr.err != SE_OK) return -13;
  return run_capture(&cr, out, out_cap);
}

/* A multi-KB source streamed through tiny windows produces byte-identical
 * output to a whole-buffer compile, across several window sizes (varying
 * where the slides land relative to token boundaries). */
int test_srcwin_stream_matches_whole(void) {
  static const uint16_t wins[] = { 192, 256, 384, 512, 1024 };
  char expected[256], got[256];
  long src_len;
  unsigned int i;

  src_len = write_big_source(16);
  EXPECT(src_len > 2000, 1);            /* must dwarf every test window */
  EXPECT(whole_run(expected, sizeof expected) == 0, 2);

  for (i = 0; i < sizeof wins / sizeof wins[0]; ++i) {
    int rc = stream_run(wins[i], got, sizeof got, NULL);
    if (rc != 0) return 1000 * (int)(i + 1) + (-rc);
    EXPECT(strcmp(got, expected) == 0, 200 + (int)i);
  }
  return 0;
}

/* A small file (shorter than the window) streams trivially: the open
 * fill consumes the whole file and sets eof. */
int test_srcwin_small_file(void) {
  char expected[64], got[64];
  unsigned char eof = 0;
  FILE *f = fopen(k_src_path, "wb");
  EXPECT(f != NULL, 1);
  fputs("var a = 40\na = a + 2\nprint(a)\n", f);
  fclose(f);

  EXPECT(whole_run(expected, sizeof expected) == 0, 2);
  EXPECT(stream_run(512, got, sizeof got, &eof) == 0, 3);
  EXPECT(eof == 1, 4);
  EXPECT(strcmp(got, expected) == 0, 5);
  return 0;
}

/* One statement longer than the window (a ~400-byte expression — a long
 * string literal would trip the 256 B string scratch even whole-buffer):
 * the whole-buffer compile of the same file succeeds; the streamed
 * compile must leave eof == 0 (file not consumed). NOTE the parse itself
 * may "succeed" — the lexer EOFs mid-expression at the window edge and
 * the partial statement can still parse cleanly (silent truncation!) —
 * which is exactly why compiler_main rejects on !eof even when the
 * compile reports success. The eof flag, not the error code, is the
 * contract under test. */
int test_srcwin_statement_too_long(void) {
  char expected[64], got[64];
  unsigned char eof = 1;
  int i;
  FILE *f = fopen(k_src_path, "wb");
  EXPECT(f != NULL, 1);
  fputs("var a = 0\na = 1", f);
  for (i = 0; i < 100; ++i) fputs(" + 1", f);
  fputs("\nprint(a)\n", f);
  fclose(f);

  EXPECT(whole_run(expected, sizeof expected) == 0, 2);
  EXPECT(strcmp(expected, "101\n") == 0, 3);
  (void)stream_run(256, got, sizeof got, &eof);
  EXPECT(eof == 0, 4);
  return 0;
}
