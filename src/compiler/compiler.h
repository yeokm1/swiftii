/* Compiler entry points.
 *
 * One pass over source produces a bytecode buffer ready for the VM.
 * Globals defined during compilation persist in the globals table until
 * `globals_reset()` is called — file_runner clears between programs;
 * the REPL keeps state across input lines.
 */
#ifndef SWIFTII_COMPILER_H
#define SWIFTII_COMPILER_H

#include <stdint.h>
#include "../common/errors.h"

typedef struct compile_result {
  swiftii_err_t err;
  const char *err_msg;
  uint16_t err_line;
  /* Total bytes used in the shared bytecode buffer after compile,
   * counting both the persistent function arena and the top-level
   * scratch. The VM bounds-checks PC against this value. */
  uint16_t bc_len;
  /* Offset within the bytecode buffer where top-level execution
   * starts. Equals `bcbuf_arena_used()` at compile time. earlier
   * builds executed from offset 0 implicitly; the split layout
   * needs the VM to jump over the function arena. */
  uint16_t program_start;
} CompileResult;

/* Compile `src` (length `len`) into the shared bytecode buffer
 * (`bcbuf_data()`). Output is written via `out`. File mode: bare
 * top-level expressions are evaluated and the result is discarded.
 *
 * The split-buffer layout means callers no longer pass their
 * own buffer — the compiler always writes into `bcbuf` so the
 * persistent function arena and the per-line scratch share one
 * physical region. */
void compiler_compile_source(const char *src, uint16_t len,
                    CompileResult *out);

/* Same as compiler_compile_source, but compiles in REPL mode: a bare top-level
 * expression statement (anything that isn't `let`/`var`/assignment or
 * an explicit `print(...)` call) is wrapped in an implicit print so
 * the user sees its value. File-mode compilation discards the value. */
void compiler_compile_source_repl(const char *src, uint16_t len,
                         CompileResult *out);

#ifdef WITH_SWB
/* (Doc 016): register a streaming-source refill hook
 * (srcwin_refill + its SrcWin) for subsequent compiles; `src`/`len` then
 * describe the initially-filled window and the hook pulls the rest from
 * disk at statement boundaries. Sticky until reset — pass NULL/NULL to
 * return to whole-buffer compiles. Family B Compiler + host tests only. */
struct parser;
void compiler_set_refill(void (*fn)(struct parser *p), void *ctx);
#endif

#endif /* SWIFTII_COMPILER_H */
