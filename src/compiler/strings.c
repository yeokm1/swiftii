/* String literal compilation: escapes and interpolation.
 *
 * The lexer hands us a TOK_STR whose tok_pos/tok_len bracket the raw
 * source bytes between the surrounding quotes (escape sequences and
 * `\(...)` interpolations included). This compiler walks those bytes
 * once, accumulating literal text into a scratch buffer (processing
 * `\\`, `\"`, `\n`, `\t`, `\r`, `\0`); when it hits `\(`, it flushes
 * the current literal piece as an OP_STR <heap_off>, sub-lexes the
 * interpolated expression, emits `OP_STR_INTERP_I`, then concatenates
 * with the running result via OP_ADD (string + string, dispatched by
 * the VM).
 *
 * Heap allocation for compile-time string constants happens *during*
 * compilation. Each literal piece is heap_alloc'd with refcount 1
 * (the "constant pool" reference); OP_STR retains on push and the
 * stack ops release on pop, so the constant survives across program
 * runs in the REPL.
 *
 * Sub-lexing for `\(expr)`: we save the parser's lexer state, point a
 * fresh lexer at the inner span, parse one expression, restore the
 * outer lexer. The inner lexer can recursively encounter strings of
 * its own — see the limitation in the lexer that nested
 * string literals inside an interpolation are not allowed.
 */
#include "parser.h"

#include <stdint.h>
#include "../common/config.h"
#include "../common/errors.h"
#include "../lexer/lexer.h"
#include "../lexer/tokens.h"
#include "../runtime/heap.h"
#include "../vm/opcodes.h"

/* Scratch for processed literal text. Bounded by FILE_SRC_SIZE so we
 * can't exceed the source buffer. Static to keep it off the recursive
 * pratt.c stack — string compilation is not re-entrant. */
#define STR_SCRATCH_SIZE 256
static unsigned char s_str_scratch[STR_SCRATCH_SIZE];

/* Emit OP_STR for a literal piece by allocating it on the heap and
 * recording the offset as the operand. Returns 1 on success, 0 on
 * heap exhaustion. */
static int emit_literal_piece(Parser *p, const unsigned char *bytes,
                              uint16_t len) {
  heap_off_t off;
  unsigned char *dst;
  uint16_t i;

  off = heap_alloc(len);
  if (off == HEAP_NULL) {
    parser_fail(p, SE_OOM, "heap full");
    return 0;
  }
  dst = heap_payload(off);
  for (i = 0; i < len; ++i) dst[i] = bytes[i];
  emit_op_u16(p, OP_STR, (uint16_t)off);
  return 1;
}

/* Sub-lex and compile the interpolation expression spanning
 * src[start..end). Saves and restores the outer lexer state. */
static void compile_interp_expr(Parser *p, uint16_t start, uint16_t end) {
  Lexer saved;
  if (end <= start) {
    parser_fail(p, SE_BAD_OPCODE, "empty interpolation");
    return;
  }
  saved = p->L;
  /* The sub-lexer scans the same source buffer but is constrained to
   * the inner expression span by length. */
  lexer_init(&p->L, p->L.src, end);
  p->L.pos = start;
  lexer_next(&p->L);
  parse_expression(p);
  if (!p->err && p->L.tok != TOK_EOF) {
    parser_fail(p, SE_BAD_OPCODE, "bad interp");
  }
  p->L = saved;
}

void compile_string_literal(Parser *p) {
  uint16_t raw_start;
  uint16_t raw_end;
  uint16_t i;
  uint16_t scratch_pos;
  unsigned char piece_count;
  unsigned char c;
  unsigned char e;
  const char *src;

  if (p->err) return;
  src = p->L.src;
  raw_start = p->L.tok_pos;
  raw_end = (uint16_t)(p->L.tok_pos + p->L.tok_len);
  scratch_pos = 0;
  piece_count = 0;

  i = raw_start;
  while (i < raw_end) {
    c = (unsigned char)src[i];
    if (c == '\\' && i + 1 < raw_end) {
      e = (unsigned char)src[i + 1];
      if (e == '(') {
        /* Find matching ')', tracking nested paren depth. */
        uint16_t expr_start;
        uint16_t depth;
        uint16_t j;
        expr_start = (uint16_t)(i + 2);
        depth = 1;
        j = expr_start;
        while (j < raw_end && depth > 0) {
          c = (unsigned char)src[j];
          if (c == '(') ++depth;
          else if (c == ')') {
            --depth;
            if (depth == 0) break;
          }
          ++j;
        }
        if (depth != 0) {
          parser_fail(p, SE_BAD_OPCODE,
                      "unterminated interp");
          return;
        }
        /* Flush accumulated literal text, if any. */
        if (scratch_pos > 0) {
          if (!emit_literal_piece(p, s_str_scratch, scratch_pos)) return;
          if (piece_count > 0) emit_op(p, OP_ADD);
          ++piece_count;
          scratch_pos = 0;
        }
        /* Compile the inner expression then convert to string. */
        compile_interp_expr(p, expr_start, j);
        if (p->err) return;
        emit_op(p, OP_STR_INTERP_I);
        if (piece_count > 0) emit_op(p, OP_ADD);
        ++piece_count;
        i = (uint16_t)(j + 1);  /* skip past ')' */
        continue;
      }
      /* Regular escape character. */
      if (scratch_pos >= STR_SCRATCH_SIZE) {
        parser_fail(p, SE_OOM, "string too long");
        return;
      }
      switch (e) {
        case '\\': s_str_scratch[scratch_pos++] = '\\'; break;
        case '"':  s_str_scratch[scratch_pos++] = '"';  break;
        case 'n':  s_str_scratch[scratch_pos++] = '\n'; break;
        case 't':  s_str_scratch[scratch_pos++] = '\t'; break;
        case 'r':  s_str_scratch[scratch_pos++] = '\r'; break;
        case '0':  s_str_scratch[scratch_pos++] = '\0'; break;
        default:
          parser_fail(p, SE_BAD_OPCODE, "bad escape");
          return;
      }
      i += 2;
      continue;
    }
    if (scratch_pos >= STR_SCRATCH_SIZE) {
      parser_fail(p, SE_OOM, "string too long");
      return;
    }
    s_str_scratch[scratch_pos++] = c;
    ++i;
  }

  /* Flush the trailing literal piece. If the string is empty AND we
   * never emitted any pieces, emit one empty OP_STR so the caller
   * always sees a Value on the stack. */
  if (scratch_pos > 0) {
    if (!emit_literal_piece(p, s_str_scratch, scratch_pos)) return;
    if (piece_count > 0) emit_op(p, OP_ADD);
    ++piece_count;
  } else if (piece_count == 0) {
    if (!emit_literal_piece(p, s_str_scratch, 0)) return;
    ++piece_count;
  }

  lexer_next(&p->L);  /* consume TOK_STR */
}
