/* Bytecode emission helpers.
 *
 * Thin wrappers that write into the Parser's bytecode buffer with a
 * bounds check. On overflow the parser is marked failed (SE_OOM) and
 * subsequent emits are no-ops — emit_* functions therefore never need
 * to be checked individually at the call site; the final
 * `parser->err` check at compiler_compile_source() time tells you what happened.
 */
#include "parser.h"

#include <stdint.h>
#include "../vm/opcodes.h"
#include "bcbuf.h"

void emit_byte(Parser *p, unsigned char b) {
  if (p->err) return;
  if (p->bc_pos >= p->bc_cap) {
    parser_fail(p, SE_OOM, "compiled bytecode exceeds buffer");
    return;
  }
#ifdef WITH_AUX_COMPILE
  /* Paged buffer: a write may flush the frozen arena to aux; a 0 return means
   * the resident scratch alone overflows the MAIN window (program too big for
   * this machine — the top-level-heavy case the aux paging can't help). */
  if (!bcbuf_put(p->bc_pos, b)) {
    parser_fail(p, SE_OOM, "program too big for memory");
    return;
  }
  p->bc_pos++;
#else
  p->bc[p->bc_pos++] = b;
#endif
}

void emit_op(Parser *p, unsigned char op) {
  emit_byte(p, op);
}

void emit_op_u8(Parser *p, unsigned char op, unsigned char operand) {
  emit_byte(p, op);
  emit_byte(p, operand);
}

void emit_op_u16(Parser *p, unsigned char op, uint16_t operand) {
  emit_byte(p, op);
  emit_byte(p, (unsigned char)(operand & 0xFF));
  emit_byte(p, (unsigned char)((operand >> 8) & 0xFF));
}

/* Pick the smallest opcode that fits the literal.
 *  - 0..255       → OP_INT_U8  (2 bytes)
 *  - else          → OP_INT_I16 (3 bytes) */
void emit_int_literal(Parser *p, int16_t value) {
  if (value >= 0 && value <= 255) {
    emit_op_u8(p, OP_INT_U8, (unsigned char)value);
  } else {
    emit_byte(p, OP_INT_I16);
    emit_byte(p, (unsigned char)((uint16_t)value & 0xFF));
    emit_byte(p, (unsigned char)(((uint16_t)value >> 8) & 0xFF));
  }
}

uint16_t emit_jump_placeholder(Parser *p, unsigned char op) {
  uint16_t pos;
  emit_byte(p, op);
  pos = p->bc_pos;
  emit_byte(p, 0xFF);
  emit_byte(p, 0xFF);
  return pos;
}

void patch_jump_to_here(Parser *p, uint16_t placeholder_pos) {
  uint16_t after_operand;
  int16_t offset;
  if (p->err) return;
  after_operand = (uint16_t)(placeholder_pos + 2);
  offset = (int16_t)(p->bc_pos - after_operand);
  if ((uint16_t)(placeholder_pos + 1) >= p->bc_cap) return;
  /* Backpatch sites are always in resident scratch (>= arena_used >= the
   * flushed base), so BC_PUT here never flushes. */
  BC_PUT(p, placeholder_pos,     (unsigned char)((uint16_t)offset & 0xFF));
  BC_PUT(p, placeholder_pos + 1, (unsigned char)(((uint16_t)offset >> 8) & 0xFF));
}

void emit_loop(Parser *p, uint16_t loop_start) {
  uint16_t back;
  emit_byte(p, OP_LOOP);
  /* Offset is unsigned and counted from the byte right after the
   * 2-byte operand, back to `loop_start`. */
  back = (uint16_t)((p->bc_pos + 2) - loop_start);
  emit_byte(p, (unsigned char)(back & 0xFF));
  emit_byte(p, (unsigned char)((back >> 8) & 0xFF));
}

/* Re-entrant placeholder LIFO. The parsers park any bytecode-cursor value
 * that must survive a recursive parse_block here (parse_if's else_patch +
 * jump-over-else; parse_while/parse_for_in's loop_start/exit_patch/gidx;
 * parse_if_let's skip_patch/end_patch) so cc65's -Cl static locals can't be
 * clobbered by a nested if/while/for-in. See docs/contributing/LESSONS.md 2026-06-03.
 *
 * Module-static (not in the Parser struct) on purpose: there is only ever
 * one active compile, and the no-arg ABI makes each push/pop a couple of
 * bytes cheaper than passing `p` — across the ~16 call sites that pays for
 * protecting every at-risk local within the tight II+ budgets. Also factored
 * into functions, not inlined: inlining the pointer bump cost ~25 B/site and
 * overflowed the lite LC segment by ~289 B. Reset per compile by
 * patch_stack_reset() (compiler.c). Depth 24 covers demo nesting (a for-in
 * pushes 3 slots). */
static uint16_t s_patch_stack[24];
static uint16_t *s_patch_top = s_patch_stack;

void patch_stack_reset(void) {
  s_patch_top = s_patch_stack;
}
void patch_stack_push(uint16_t v) {
  *s_patch_top++ = v;
}
uint16_t patch_stack_pop(void) {
  return *--s_patch_top;
}
