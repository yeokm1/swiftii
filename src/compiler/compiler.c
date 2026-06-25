/* Top-level compiler orchestration.
 *
 * Single-pass design: initialise the Parser struct (lexer +
 * emission cursor + error slot), run the statement parser, return a
 * result with success/error and the emitted byte count.
 *
 * Split-buffer layout (REPL function persistence):
 *   bcbuf[0 .. arena_used)               persistent function arena
 *   bcbuf[arena_used .. arena_used + S)  top-level scratch (this compile)
 * Top-level statements emit forward starting at `arena_used`. `func`
 * declarations emit their bodies at the current end-of-scratch, then
 * rotate them down into the arena (see `bcbuf_rotate_func_into_arena`).
 *
 * The compiler is *not* re-entrant — there is only ever one program
 * being compiled at a time. The Parser is on the stack of
 * compiler_compile_source.
 */
#include "compiler.h"
#include "parser.h"

#include <stdint.h>

#include "bcbuf.h"
#include "funcs.h"
#include "globals.h"
#include "locals.h"
#include "loops.h"
#include "../runtime/heap.h"
#include "../vm/opcodes.h"

#ifdef WITH_SWB
/* (Doc 016): sticky streaming-refill registration, copied
 * into each compile's Parser. The compiler is single-instance and
 * non-reentrant, so module state (not a parameter) keeps the Family-A
 * compile_impl signature untouched. Callers that stream (compiler_main)
 * set it before compiling; set NULL to go back to whole-buffer mode. */
static void (*s_refill)(struct parser *p);
static void *s_refill_ctx;

void compiler_set_refill(void (*fn)(struct parser *p), void *ctx) {
  s_refill = fn;
  s_refill_ctx = ctx;
}
#endif

void parser_fail(Parser *p, swiftii_err_t err, const char *msg) {
  if (p->err) return;  /* first failure wins */
  p->err = err;
  p->err_msg = msg;
  p->err_line = p->L.tok_line;
}

void parser_expect(Parser *p, tok_t expected, const char *msg) {
  if (p->err) return;
  if (p->L.tok != expected) {
    parser_fail(p, SE_BAD_OPCODE, msg);
    return;
  }
  lexer_next(&p->L);
}

static void compile_impl(const char *src, uint16_t len,
                         CompileResult *out,
                         unsigned char repl_print_top) {
  Parser p;
  uint8_t globals_count_save;
  uint8_t funcs_count_save;
  uint16_t arena_save;
  heap_off_t heap_save;
  uint16_t program_start;

  /* Atomic-compile rollback: capture the symbol-table count, the
   * function table count, the function-arena watermark, and the heap
   * bump pointer before parsing. If parsing fails after partial
   * progress (a half-defined function, an unfinished `let y = 2.4`,
   * etc.), restoring all four leaves the session in its pre-statement
   * state. */
  globals_savepoint(&globals_count_save);
  funcs_savepoint(&funcs_count_save);
  arena_save = bcbuf_arena_used();
  heap_save = heap_savepoint();

  /* Top-level scratch initially begins at the persistent arena
   * watermark. If parse_program encounters `func` declarations the
   * arena grows; the actual top-level program-start is recorded
   * after parsing finishes (see program_start assignment below). */
  /* Loop contexts never persist across compiles — every `while` /
   * `for-in` body is entered and exited in the same parse, and an
   * imbalanced compile gets rolled back. Resetting here is defensive
   * against a previous parse that bailed out mid-loop. */
  loops_reset();

  lexer_init(&p.L, src, len);
  p.bc = bcbuf_data();
  p.bc_pos = arena_save;
#ifdef WITH_AUX_COMPILE
  /* Paged: bc_cap is the LOGICAL ceiling (the aux bytecode park), not the
   * small MAIN window — the window-full case is reported by bcbuf_put. */
  p.bc_cap = (uint16_t)AUX_BC_MAX;
#else
  p.bc_cap = bcbuf_size();
#endif
  p.err = SE_OK;
  p.err_msg = (const char *)0;
  p.err_line = 1;
  p.repl_print_top = repl_print_top;
  p.in_function = 0;
  p.fn_has_return = 0;
#ifdef WITH_SWB
  p.refill = s_refill;
  p.refill_ctx = s_refill_ctx;
#endif
  patch_stack_reset();

  parse_program(&p);

  /* After parsing, the top-level scratch lives at
   * [bcbuf_arena_used() .. p.bc_pos). The VM starts execution at
   * that offset. */
  program_start = bcbuf_arena_used();

  if (p.err != SE_OK) {
    /* Atomic rollback: drop any function entries this compile created,
     * rewind the arena watermark, the global symbol table, and the
     * heap. The scratch region (above `arena_save`) is overwritten by
     * the next compile, so no explicit zeroing is needed. */
    funcs_rollback(funcs_count_save);
    bcbuf_arena_truncate(arena_save);
    globals_rollback(globals_count_save);
    heap_rollback(heap_save);
  }

  out->err = p.err;
  out->err_msg = p.err_msg;
  out->err_line = p.err_line;
  out->bc_len = p.bc_pos;
  out->program_start = program_start;
}

void compiler_compile_source(const char *src, uint16_t len,
                    CompileResult *out) {
  compile_impl(src, len, out, 0);
}

void compiler_compile_source_repl(const char *src, uint16_t len,
                         CompileResult *out) {
  compile_impl(src, len, out, 1);
}
