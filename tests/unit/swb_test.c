/* `.swb` serialise/deserialise round-trip tests (doc 015).
 *
 * The host stand-in for the compiler->.swb->runner split: compile a
 * program, serialise the bytecode + constant heap + funcs table into a
 * `.swb` image, then simulate the runner — deserialise into a SEPARATE
 * bytecode buffer (rebuilding the heap + funcs singletons) and run it
 * through the VM with captured stdout. The output must match a direct
 * compile-and-run, proving the on-disk artifact is faithful.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "common/errors.h"
#include "compiler/compiler.h"
#include "compiler/globals.h"
#include "vm/vm.h"
#include "runtime/heap.h"
#include "swb/swb.h"
#include "platform/platform.h"

#define EXPECT(cond, code) do { if (!(cond)) return (code); } while (0)

extern unsigned char *bcbuf_data(void);

/* Compile `src`, serialise to `.swb`, deserialise into a fresh bytecode
 * buffer, run it, and capture stdout into `out`. Returns 0 on success or a
 * negative step code. `*swb_size` (optional) receives the image length. */
static int compile_swb_run(const char *src, char *out, size_t out_cap,
                           uint16_t *swb_size) {
  CompileResult cr;
  swb_err_t se;
  swiftii_err_t rc;
  unsigned char image[2048];
  static unsigned char runner_bc[4096];
  uint16_t img_len, prog_start, bc_len;
  int saved_fd, capture_fd;
  ssize_t n;
  const char *tmp_path = "/tmp/swiftii_swb_test.txt";

  /* Mirror file_runner's compile-driver contract (globals + VM + heap
   * reset before each program), so each `.swb` carries only its own
   * constant pool. */
  globals_reset();
  vm_reset_globals();
  heap_reset();

  compiler_compile_source(src, (uint16_t)strlen(src), &cr);
  if (cr.err != SE_OK) return -1;

  se = swb_write(bcbuf_data(), cr.bc_len, cr.program_start,
                 image, (uint16_t)sizeof image, &img_len);
  if (se != SWB_OK) return -10 - (int)se;
  if (swb_size) *swb_size = img_len;

  /* Simulate the runner in a clean state: a distinct bytecode buffer, and
   * swb_read rebuilds the heap + funcs from the image alone. */
  se = swb_read(image, img_len, runner_bc, (uint16_t)sizeof runner_bc,
                &prog_start, &bc_len);
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
  rc = vm_run(runner_bc, prog_start, bc_len);
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

/* Writer-callback sink for swb_write_stream: appends to a growable buffer. */
typedef struct { unsigned char *buf; uint16_t cap; uint16_t len; } memsink;
static int memsink_write(void *ctx, const unsigned char *src, uint16_t n) {
  memsink *s = (memsink *)ctx;
  uint16_t i;
  if ((uint32_t)s->len + n > s->cap) return -1;
  for (i = 0; i < n; ++i) s->buf[s->len++] = src[i];
  return 0;
}

/* The real compiler->runner path: stream-write the .swb, then open it in
 * place and run the bytecode without copying it out. */
static int compile_stream_run(const char *src, char *out, size_t out_cap) {
  CompileResult cr;
  swb_err_t se;
  swiftii_err_t rc;
  static unsigned char image[4096];
  const unsigned char *bc;
  memsink sink;
  uint16_t prog_start, bc_len;
  int saved_fd, capture_fd;
  ssize_t n;
  const char *tmp_path = "/tmp/swiftii_swb_stream_test.txt";

  globals_reset();
  vm_reset_globals();
  heap_reset();
  compiler_compile_source(src, (uint16_t)strlen(src), &cr);
  if (cr.err != SE_OK) return -1;

  sink.buf = image; sink.cap = (uint16_t)sizeof image; sink.len = 0;
  se = swb_write_stream(bcbuf_data(), cr.bc_len, cr.program_start,
                        memsink_write, &sink);
  if (se != SWB_OK) return -10 - (int)se;

  se = swb_open_image(image, sink.len, &bc, &prog_start, &bc_len);
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
  rc = vm_run(bc, prog_start, bc_len);  /* bytecode runs in place */
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

int test_swb_stream_roundtrip(void) {
  char buf[128];
  const char *src =
      "func sq(n: Int) -> Int { return n * n }\n"
      "print(\"sq\")\n"
      "print(sq(n: 9))\n";
  EXPECT(compile_stream_run(src, buf, sizeof buf) == 0, 1);
  EXPECT(strcmp(buf, "sq\n81\n") == 0, 2);
  return 0;
}

int test_swb_open_image_matches_read(void) {
  /* swb_open_image must point at the same bytecode swb_read would copy. */
  CompileResult cr;
  unsigned char image[1024];
  static unsigned char copied[512];
  const unsigned char *inplace;
  uint16_t img_len, ps1, bl1, ps2, bl2, i;
  globals_reset();
  vm_reset_globals();
  heap_reset();
  compiler_compile_source("print(2 + 3)\n", 12, &cr);
  EXPECT(cr.err == SE_OK, 1);
  EXPECT(swb_write(bcbuf_data(), cr.bc_len, cr.program_start,
                   image, sizeof image, &img_len) == SWB_OK, 2);
  EXPECT(swb_read(image, img_len, copied, sizeof copied, &ps1, &bl1) == SWB_OK, 3);
  EXPECT(swb_open_image(image, img_len, &inplace, &ps2, &bl2) == SWB_OK, 4);
  EXPECT(ps1 == ps2 && bl1 == bl2, 5);
  for (i = 0; i < bl1; ++i) EXPECT(copied[i] == inplace[i], 6);
  return 0;
}

int test_swb_roundtrip_arithmetic(void) {
  char buf[64];
  EXPECT(compile_swb_run("print(21 * 2)\n", buf, sizeof buf, NULL) == 0, 1);
  EXPECT(strcmp(buf, "42\n") == 0, 2);
  return 0;
}

int test_swb_roundtrip_string_const(void) {
  /* Exercises the constant-heap section: the string literal lives in the
   * heap and is reproduced byte-for-byte on the runner side. */
  char buf[64];
  EXPECT(compile_swb_run("print(\"hello\")\n", buf, sizeof buf, NULL) == 0, 1);
  EXPECT(strcmp(buf, "hello\n") == 0, 2);
  return 0;
}

int test_swb_roundtrip_func_call(void) {
  /* Exercises the funcs section: OP_CALL resolves the target via the
   * rebuilt funcs table on the runner side. */
  char buf[64];
  const char *src =
      "func sq(n: Int) -> Int { return n * n }\n"
      "print(sq(n: 7))\n";
  EXPECT(compile_swb_run(src, buf, sizeof buf, NULL) == 0, 1);
  EXPECT(strcmp(buf, "49\n") == 0, 2);
  return 0;
}

int test_swb_roundtrip_mixed(void) {
  /* Functions + strings + a loop + an array in one program. */
  char buf[128];
  const char *src =
      "func dbl(n: Int) -> Int { return n + n }\n"
      "var xs = [1, 2, 3]\n"
      "var total = 0\n"
      "for i in 0..<xs.count { total = total + dbl(n: xs[i]) }\n"
      "print(\"sum\")\n"
      "print(total)\n";
  EXPECT(compile_swb_run(src, buf, sizeof buf, NULL) == 0, 1);
  EXPECT(strcmp(buf, "sum\n12\n") == 0, 2);
  return 0;
}

int test_swb_image_has_header(void) {
  char buf[64];
  uint16_t sz = 0;
  EXPECT(compile_swb_run("print(1)\n", buf, sizeof buf, &sz) == 0, 1);
  EXPECT(sz >= SWB_HEADER_SIZE, 2);
  return 0;
}

int test_swb_bad_magic(void) {
  unsigned char img[32];
  static unsigned char bc[64];
  uint16_t ps, bl;
  memset(img, 0, sizeof img);
  img[0] = 'X'; img[1] = 'W'; img[2] = 'B'; img[3] = (unsigned char)SWB_VERSION;
  EXPECT(swb_read(img, sizeof img, bc, sizeof bc, &ps, &bl) == SWB_ERR_MAGIC, 1);
  return 0;
}

int test_swb_bad_version(void) {
  unsigned char img[32];
  static unsigned char bc[64];
  uint16_t ps, bl;
  memset(img, 0, sizeof img);
  img[0] = 'S'; img[1] = 'W'; img[2] = 'B'; img[3] = 99;
  EXPECT(swb_read(img, sizeof img, bc, sizeof bc, &ps, &bl) == SWB_ERR_MAGIC, 1);
  return 0;
}

int test_swb_truncated_header(void) {
  unsigned char img[4] = { 'S', 'W', 'B', (unsigned char)SWB_VERSION };
  static unsigned char bc[64];
  uint16_t ps, bl;
  EXPECT(swb_read(img, sizeof img, bc, sizeof bc, &ps, &bl) == SWB_ERR_TRUNC, 1);
  return 0;
}

int test_swb_truncated_body(void) {
  /* Header claims bc_len 100 but the image is only the header. */
  unsigned char img[SWB_HEADER_SIZE];
  static unsigned char bc[256];
  uint16_t ps, bl;
  memset(img, 0, sizeof img);
  img[0] = 'S'; img[1] = 'W'; img[2] = 'B'; img[3] = (unsigned char)SWB_VERSION;
  img[6] = 100; img[7] = 0;  /* bc_len = 100, no body present */
  EXPECT(swb_read(img, sizeof img, bc, sizeof bc, &ps, &bl) == SWB_ERR_TRUNC, 1);
  return 0;
}

int test_swb_bc_cap(void) {
  /* A valid small program won't fit a 1-byte bytecode buffer. */
  CompileResult cr;
  unsigned char image[512];
  unsigned char tiny[1];
  uint16_t img_len, ps, bl;
  globals_reset();
  vm_reset_globals();
  heap_reset();
  compiler_compile_source("print(1 + 2)\n", 12, &cr);
  EXPECT(cr.err == SE_OK, 1);
  EXPECT(swb_write(bcbuf_data(), cr.bc_len, cr.program_start,
                   image, sizeof image, &img_len) == SWB_OK, 2);
  EXPECT(swb_read(image, img_len, tiny, sizeof tiny, &ps, &bl)
         == SWB_ERR_BC_CAP, 3);
  return 0;
}

int test_swb_roundtrip_extras_builtins(void) {
  /* The extras surface (XLC-id builtins + array methods) must survive the
   * compile -> .swb -> run round trip — the Family B Compiler recognises
   * them and the Runner dispatches them (2026-06-12 fix: both halves were
   * cc65-gated out of Family B, invisible to host tests because every
   * gate includes !__CC65__). Graphics builtins are host no-ops, so
   * exercise the testable ones: asc/chr/Int(_:) + removeLast. `wait()` and
   * `tone()` are also Family B builtins (host no-ops); including them proves
   * they survive recognition -> .swb serialization -> Runner dispatch without
   * disturbing the surrounding output. abs/sgn (pure Int math) and
   * hasPrefix/hasSuffix (String -> Bool, folded into the array-method
   * recognizer) are likewise Family B only and return real values, so they
   * also assert correct results. */
  char out[256];
  int rc = compile_swb_run(
      "print(asc(\"A\"))\n"
      "wait(5)\n"
      "tone(40, 3)\n"
      "print(chr(66))\n"
      "if let n = Int(\"123\") { print(n) }\n"
      "var xs = [1, 2, 3]\n"
      "let last = xs.removeLast()\n"
      "print(last)\n"
      "print(xs.count)\n"
      "print(abs(0 - 7))\n"
      "print(abs(9))\n"
      "print(sgn(0 - 4))\n"
      "print(sgn(0))\n"
      "print(sgn(12))\n"
      "print(\"hello.swift\".hasSuffix(\".swift\"))\n"
      "print(\"hello\".hasPrefix(\"he\"))\n"
      "print(\"hello\".hasPrefix(\"xy\"))\n",
      out, sizeof out, 0);
  EXPECT(rc == 0, 1);
  EXPECT(strcmp(out,
                "65\nB\n123\n3\n2\n7\n9\n-1\n0\n1\ntrue\ntrue\nfalse\n") == 0,
         2);
  return 0;
}

int test_swb_bad_program_start(void) {
  /* Entry PC past the end of the bytecode -> SWB_ERR_BOUNDS (a corrupt
   * image must not "run" as a silent no-op). program_start == bc_len is
   * legal (empty top-level), so use bc_len + 1. */
  unsigned char img[SWB_HEADER_SIZE + 4];
  const unsigned char *bc;
  uint16_t ps, bl;
  memset(img, 0, sizeof img);
  img[0] = 'S'; img[1] = 'W'; img[2] = 'B'; img[3] = (unsigned char)SWB_VERSION;
  img[4] = 5; img[5] = 0;    /* program_start = 5 */
  img[6] = 4; img[7] = 0;    /* bc_len = 4 */
  EXPECT(swb_open_image(img, sizeof img, &bc, &ps, &bl) == SWB_ERR_BOUNDS, 1);
  return 0;
}

int test_swb_bad_func_start(void) {
  /* A funcs-table entry whose start PC lies outside the bytecode ->
   * SWB_ERR_BOUNDS (start == bc_len is already past the last instruction). */
  unsigned char img[SWB_HEADER_SIZE + 4 + SWB_FUNC_SIZE];
  const unsigned char *bc;
  uint16_t ps, bl;
  memset(img, 0, sizeof img);
  img[0] = 'S'; img[1] = 'W'; img[2] = 'B'; img[3] = (unsigned char)SWB_VERSION;
  img[6] = 4; img[7] = 0;    /* bc_len = 4, program_start = 0 */
  img[10] = 1;               /* funcs_count = 1 */
  img[SWB_HEADER_SIZE + 4 + 0] = 4;  /* func 0 start = 4 == bc_len: bad */
  img[SWB_HEADER_SIZE + 4 + 1] = 0;
  EXPECT(swb_open_image(img, sizeof img, &bc, &ps, &bl) == SWB_ERR_BOUNDS, 1);
  return 0;
}

int test_swb_out_full(void) {
  /* Serialising into too small an output buffer is rejected. */
  CompileResult cr;
  unsigned char tiny[4];
  uint16_t img_len;
  globals_reset();
  vm_reset_globals();
  heap_reset();
  compiler_compile_source("print(1 + 2)\n", 12, &cr);
  EXPECT(cr.err == SE_OK, 1);
  EXPECT(swb_write(bcbuf_data(), cr.bc_len, cr.program_start,
                   tiny, sizeof tiny, &img_len) == SWB_ERR_OUT_FULL, 2);
  return 0;
}
