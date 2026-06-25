/* Family B file I/O tests (doc 015).
 *
 * Two layers: (1) the userfile_read/write module directly (round-trip a
 * temp file); (2) the compiler recognises readFile/writeFile and emits the
 * right OP_CALL_BUILTIN ids. The VM dispatch itself is the Runner's lite
 * path (verified on the emulator) — on host the builtin id routes through
 * the XLC stub, so we assert the emitted bytecode here rather than running.
 */

#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "common/config.h"
#include "common/errors.h"
#include "compiler/compiler.h"
#include "compiler/globals.h"
#include "compiler/bcbuf.h"
#include "vm/vm.h"
#include "vm/opcodes.h"
#include "runtime/heap.h"
#include "runtime/file_io.h"
#include "runtime/prodos.h"

#define EXPECT(cond, code) do { if (!(cond)) return (code); } while (0)

int test_userfile_write_then_read(void) {
  const char *path = "/tmp/swiftii_userfile_test.txt";
  const unsigned char payload[] = "hello, file\nsecond line\n";
  const unsigned char *out;
  int16_t n;
  uint16_t len = (uint16_t)(sizeof payload - 1);

  EXPECT(userfile_write(path, payload, len) == 0, 1);
  n = userfile_read(path, &out);
  EXPECT(n == (int16_t)len, 2);
  EXPECT(memcmp(out, payload, len) == 0, 3);
  return 0;
}

int test_userfile_read_missing(void) {
  const unsigned char *out;
  EXPECT(userfile_read("/tmp/swiftii_does_not_exist_42.txt", &out) == -1, 1);
  return 0;
}

int test_userfile_empty(void) {
  const char *path = "/tmp/swiftii_userfile_empty.txt";
  const unsigned char *out;
  EXPECT(userfile_write(path, (const unsigned char *)"", 0) == 0, 1);
  EXPECT(userfile_read(path, &out) == 0, 2);
  return 0;
}

/* Find OP_CALL_BUILTIN <id> in the freshly compiled bytecode. */
static int bc_has_builtin(unsigned char id) {
  const unsigned char *bc = bcbuf_data();
  uint16_t i;
  for (i = 0; i + 2 < (uint16_t)FILE_BC_SIZE; ++i) {
    if (bc[i] == OP_CALL_BUILTIN && bc[i + 1] == id) return 1;
  }
  return 0;
}

int test_compile_read_file(void) {
  CompileResult cr;
  const char *src = "let s = readFile(\"DATA.TXT\")\n";
  globals_reset();
  vm_reset_globals();
  heap_reset();
  compiler_compile_source(src, (uint16_t)strlen(src), &cr);
  EXPECT(cr.err == SE_OK, 1);
  EXPECT(bc_has_builtin(BUILTIN_READ_FILE), 2);
  return 0;
}

int test_compile_write_file(void) {
  CompileResult cr;
  const char *src = "let ok = writeFile(\"OUT.TXT\", \"hi\")\n";
  globals_reset();
  vm_reset_globals();
  heap_reset();
  compiler_compile_source(src, (uint16_t)strlen(src), &cr);
  EXPECT(cr.err == SE_OK, 1);
  EXPECT(bc_has_builtin(BUILTIN_WRITE_FILE), 2);
  return 0;
}

int test_compile_write_file_wrong_arity(void) {
  /* writeFile needs two args; one should be a compile error. */
  CompileResult cr;
  const char *src = "let ok = writeFile(\"OUT.TXT\")\n";
  globals_reset();
  vm_reset_globals();
  heap_reset();
  compiler_compile_source(src, (uint16_t)strlen(src), &cr);
  EXPECT(cr.err != SE_OK, 1);
  return 0;
}

/* ---- doc 017: file/dir CRUD ---- */

int test_userfile_append_creates_and_grows(void) {
  const char *path = "/tmp/swiftii_append_test.txt";
  const unsigned char *out;
  remove(path);
  EXPECT(userfile_append(path, (const unsigned char *)"aa", 2) == 0, 1);
  EXPECT(userfile_append(path, (const unsigned char *)"bb", 2) == 0, 2);
  EXPECT(userfile_read(path, &out) == 4, 3);
  EXPECT(memcmp(out, "aabb", 4) == 0, 4);
  return 0;
}

int test_pf_delete_and_exists(void) {
  const char *path = "/tmp/swiftii_del_test.txt";
  EXPECT(userfile_write(path, (const unsigned char *)"x", 1) == 0, 1);
  EXPECT(pf_exists(path) == 1, 2);
  EXPECT(pf_delete(path) == 0, 3);
  EXPECT(pf_exists(path) == 0, 4);
  EXPECT(pf_delete(path) == -1, 5);   /* gone now */
  return 0;
}

int test_pf_rename(void) {
  const char *a = "/tmp/swiftii_ren_a.txt";
  const char *b = "/tmp/swiftii_ren_b.txt";
  const unsigned char *out;
  remove(b);
  EXPECT(userfile_write(a, (const unsigned char *)"hi", 2) == 0, 1);
  EXPECT(pf_rename(a, b) == 0, 2);
  EXPECT(pf_exists(a) == 0, 3);
  EXPECT(userfile_read(b, &out) == 2, 4);
  EXPECT(memcmp(out, "hi", 2) == 0, 5);
  remove(b);
  return 0;
}

int test_pf_mkdir_and_list(void) {
  const char *dir = "/tmp/swiftii_dir_test";
  const char *f1 = "/tmp/swiftii_dir_test/ALPHA.TXT";
  const char *f2 = "/tmp/swiftii_dir_test/BETA.TXT";
  char name[16];
  unsigned char nl;
  int saw_alpha = 0, saw_beta = 0, count = 0;

  /* Clean any prior run, then create the directory + two files. */
  remove(f1); remove(f2); pf_delete(dir);
  EXPECT(pf_mkdir(dir) == 0, 1);
  EXPECT(pf_exists(dir) == 1, 2);
  EXPECT(userfile_write(f1, (const unsigned char *)"a", 1) == 0, 3);
  EXPECT(userfile_write(f2, (const unsigned char *)"b", 1) == 0, 4);

  EXPECT(userdir_open(dir) == 0, 5);
  while (userdir_next(name, &nl)) {
    if (strcmp(name, "ALPHA.TXT") == 0) saw_alpha = 1;
    if (strcmp(name, "BETA.TXT") == 0) saw_beta = 1;
    ++count;
  }
  userdir_close();
  EXPECT(saw_alpha && saw_beta, 6);
  EXPECT(count == 2, 7);

  remove(f1); remove(f2); pf_delete(dir);
  return 0;
}

int test_userdir_open_missing(void) {
  EXPECT(userdir_open("/tmp/swiftii_no_such_dir_42") == -1, 1);
  return 0;
}

int test_compile_list_directory_type(void) {
  /* listDirectory must compile and bind to a [String]. (Direct array
   * for-in isn't in v1; programs walk by index range.) */
  CompileResult cr;
  const char *src = "let xs: [String] = listDirectory(\"/\")\n";
  globals_reset();
  vm_reset_globals();
  heap_reset();
  compiler_compile_source(src, (uint16_t)strlen(src), &cr);
  EXPECT(cr.err == SE_OK, 1);
  EXPECT(bc_has_builtin(BUILTIN_LIST_DIR), 2);
  return 0;
}

int test_compile_delete_directory_alias(void) {
  /* deleteDirectory shares deleteFile's id ($28). */
  CompileResult cr;
  const char *src = "let ok = deleteDirectory(\"OLD\")\n";
  globals_reset();
  vm_reset_globals();
  heap_reset();
  compiler_compile_source(src, (uint16_t)strlen(src), &cr);
  EXPECT(cr.err == SE_OK, 1);
  EXPECT(bc_has_builtin(BUILTIN_DELETE_FILE), 2);
  return 0;
}

/* Compile `src` and run it through the VM (host executes the file builtins
 * directly since doc 017). Returns the VM error. */
static swiftii_err_t compile_and_run(const char *src) {
  CompileResult cr;
  globals_reset();
  vm_reset_globals();
  heap_reset();
  compiler_compile_source(src, (uint16_t)strlen(src), &cr);
  if (cr.err != SE_OK) return cr.err;
  return vm_run(bcbuf_data(), cr.program_start, cr.bc_len);
}

int test_run_write_then_read_roundtrip(void) {
  /* End-to-end through the VM: writeFile then readFile, asserting the
   * read-back length via .count (string `==` is reference identity, so a
   * heap string never equals a pool literal — use the length instead). */
  const char *path = "/tmp/swiftii_e2e_rw.txt";
  Value n;
  const char *src =
    "let ok = writeFile(\"/tmp/swiftii_e2e_rw.txt\", \"payload-123\")\n"
    "var len = 0\n"
    "if let s = readFile(\"/tmp/swiftii_e2e_rw.txt\") {\n"
    "  len = s.count\n"
    "}\n";
  remove(path);
  EXPECT(compile_and_run(src) == SE_OK, 1);
  /* globals: ok=0, len=1 */
  EXPECT(vm_get_global(1, &n) == 1, 2);
  EXPECT(n.tag == T_INT && n.lo == 11 && n.hi == 0, 3);  /* "payload-123" */
  remove(path);
  return 0;
}

int test_run_append_and_delete(void) {
  const char *path = "/tmp/swiftii_e2e_ad.txt";
  const unsigned char *out;
  Value gone;
  const char *src =
    "let a = writeFile(\"/tmp/swiftii_e2e_ad.txt\", \"x\")\n"
    "let b = appendFile(\"/tmp/swiftii_e2e_ad.txt\", \"y\")\n"
    "let removed = deleteFile(\"/tmp/swiftii_e2e_ad.txt\")\n"
    "let stillThere = fileExists(\"/tmp/swiftii_e2e_ad.txt\")\n";
  remove(path);
  /* Write+append first, read the bytes, THEN let the program delete it.
   * Easiest: run it and assert the deletion + existence flags. */
  EXPECT(compile_and_run(src) == SE_OK, 1);
  /* globals: a=0, b=1, removed=2, stillThere=3 */
  EXPECT(vm_get_global(2, &gone) == 1, 2);
  EXPECT(gone.tag == T_BOOL && gone.lo == 1, 3);   /* removed == true */
  EXPECT(vm_get_global(3, &gone) == 1, 4);
  EXPECT(gone.tag == T_BOOL && gone.lo == 0, 5);   /* fileExists == false */
  EXPECT(pf_exists(path) == 0, 6);
  (void)out;
  return 0;
}
