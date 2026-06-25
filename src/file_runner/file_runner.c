/* File-mode driver.
 *
 * Loads a source file via the platform layer (host: stdio; Apple II:
 * ProDOS MLI — see platform_read_file), then compiles and runs it. Resets
 * globals / VM / heap first, so a file runs in a clean state.
 *
 * Made this platform-agnostic. Before that the module was
 * host-only behind a __CC65__ guard, because its fopen/fread calls dragged
 * roughly 2 KB of cc65 stdio runtime into the target binary; the MLI
 * backend in src/runtime/prodos.c + src/platform/apple2/mli.s replaces that.
 * Source files
 * larger than FILE_SRC_SIZE bytes are rejected (block-streaming for large
 * programs is still a carry-over).
 *
 * `.swift` files on disk are canonical lowercase ASCII regardless of the
 * authoring machine (rev 3 of design doc 003 moves the //+ input-method
 * translation upstream to src/platform/apple2/input.c). The lexer expects
 * canonical bytes, so we do not retranslate here.
 *
 * NOTE (gate, 2026-06-02): the cc65 build is still guarded out.
 * Measuring the target loading path showed it overflows the II+ lite
 * binary (SWIFTIIP) by ~804 B of CODE + ~1239 B of BSS — see the
 * gate finding. Re-enabling on target is pending the binary-scope decision
 * (which binaries host interpreter-side loading). The host build below is
 * live and drives the integration tests. */
#ifndef __CC65__

#include "file_runner.h"

#include <stdint.h>

#include "../common/config.h"
#include "../common/errors.h"
#include "../compiler/bcbuf.h"
#include "../compiler/compiler.h"
#include "../compiler/globals.h"
#include "../runtime/heap.h"
#include "../vm/vm.h"
#include "../platform/platform.h"

static char s_src[FILE_SRC_SIZE];

static void report_error(const char *prefix, const char *msg,
                         uint16_t line) {
  char numbuf[8];
  int idx;
  uint16_t n;

  platform_write_str(prefix);
  platform_write_str(": ");
  if (line > 0) {
    platform_write_str("line ");
    idx = (int)(sizeof(numbuf) - 1);
    numbuf[idx] = '\0';
    n = line;
    if (n == 0) {
      numbuf[--idx] = '0';
    } else {
      while (n > 0) {
        numbuf[--idx] = (char)('0' + (n % 10));
        n = (uint16_t)(n / 10);
      }
    }
    platform_write_str(&numbuf[idx]);
    platform_write_str(": ");
  }
  if (msg) platform_write_str(msg);
  platform_putc('\n');
}

int file_runner_run(const char *path) {
  uint16_t n = 0;
  CompileResult cr;
  swiftii_err_t rc;
  uint8_t frc;

  frc = platform_read_file(path, s_src, (uint16_t)sizeof(s_src), &n);
  if (frc != 0) {
    report_error("error",
                 frc == 2 ? "source file exceeds FILE_SRC_SIZE"
                          : "could not open source file",
                 0);
    return 1;
  }

  globals_reset();
  vm_reset_globals();
  heap_reset();

  compiler_compile_source(s_src, n, &cr);
  if (cr.err != SE_OK) {
    report_error("compile error", cr.err_msg, cr.err_line);
    return 2;
  }

  rc = vm_run(bcbuf_data(), cr.program_start, cr.bc_len);
  if (rc != SE_OK) {
    report_error("runtime error", "VM halted with error", 0);
    return 3;
  }
  return 0;
}

#endif /* !__CC65__ */
