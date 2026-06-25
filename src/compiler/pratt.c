/* Pratt expression parser.
 *
 * Operator precedence (low → high), subset:
 *   PREC_NONE       — sentinel
 *   PREC_EQUALITY   == !=
 *   PREC_COMPARISON < <= > >=
 *   PREC_TERM       + -
 *   PREC_FACTOR     * / %
 *   PREC_UNARY      - !    (prefix only)
 *   PREC_PRIMARY    literals, identifiers, ( ... )
 *
 * Does NOT support: && || ??, function calls beyond
 * the print() statement special-case, member access (.), subscripting,
 * string concatenation/interpolation.
 *
 * The parser emits bytecode directly; no AST is built.
 *
 * IMPORTANT: this file must NOT be compiled with cc65's `static-locals`
 * optimization. The Pratt parser is mutually recursive
 * (parse_primary ↔ parse_infix_loop), and static locals are shared
 * across recursive calls — the inner call's `op` would clobber the
 * outer's, causing the wrong opcode to be emitted (e.g. OP_TRAP instead
 * of OP_MUL for `x * y`). The Makefile passes `-Cl` globally for size,
 * so we override per-file with the pragma below. See docs/contributing/LESSONS.md
 * 2026-05-15 § "cc65 -Cl breaks recursive parsers".
 */
#ifdef __CC65__
#pragma static-locals (off)
/* Compile-time only; relocated to LC. Prep: this file is also
 * compiled with `--rodata-name LC` from the Makefile so cc65-generated
 * string literals (which ignore the `rodata-name` pragma) move to LC
 * too. */
#pragma code-name (push, "LC")
#endif

#include "parser.h"

#include <stdint.h>
#include "../lexer/tokens.h"
#include "../vm/opcodes.h"
#include "funcs.h"
#include "globals.h"
#include "locals.h"

/* Defined in statements.c; consumes the `(args)` for a function call
 * and emits OP_CALL. */
void parse_call_arglist_emit(Parser *p, uint8_t fn_idx);

typedef unsigned char prec_t;

#define PREC_NONE        0
#define PREC_EQUALITY    1
#define PREC_COALESCE    2   /* ??  (between equality and comparison per Swift) */
#define PREC_COMPARISON  3
#define PREC_TERM        4
#define PREC_FACTOR      5
#define PREC_UNARY       6
#define PREC_PRIMARY     7

static prec_t infix_prec(tok_t t) {
  switch (t) {
    case TOK_EQ:
    case TOK_NEQ:    return PREC_EQUALITY;
    case TOK_QQ:     return PREC_COALESCE;
    case TOK_LT:
    case TOK_LE:
    case TOK_GT:
    case TOK_GE:     return PREC_COMPARISON;
    case TOK_PLUS:
    case TOK_MINUS:  return PREC_TERM;
    case TOK_STAR:
    case TOK_SLASH:
    case TOK_PERCENT:return PREC_FACTOR;
    default:         return PREC_NONE;
  }
}

static unsigned char opcode_for_binop(tok_t t) {
  switch (t) {
    case TOK_PLUS:    return OP_ADD;
    case TOK_MINUS:   return OP_SUB;
    case TOK_STAR:    return OP_MUL;
    case TOK_SLASH:   return OP_DIV;
    case TOK_PERCENT: return OP_MOD;
    case TOK_EQ:      return OP_EQ;
    case TOK_NEQ:     return OP_NEQ;
    case TOK_LT:      return OP_LT;
    case TOK_LE:      return OP_LE;
    case TOK_GT:      return OP_GT;
    case TOK_GE:      return OP_GE;
    default:          return OP_TRAP;
  }
}

static void parse_infix_loop(Parser *p, prec_t min_prec);

static void parse_primary(Parser *p) {
  tok_t t;
  int16_t v;
  int16_t gidx;

  if (p->err) return;
  t = p->L.tok;

  switch (t) {
    case TOK_INT:
      v = p->L.int_val;
      emit_int_literal(p, v);
      comp_set_expr_ctype(CT_INT);
      lexer_next(&p->L);
      return;

    case TOK_TRUE:
      emit_op(p, OP_TRUE);
      comp_set_expr_ctype(CT_BOOL);
      lexer_next(&p->L);
      return;

    case TOK_FALSE:
      emit_op(p, OP_FALSE);
      comp_set_expr_ctype(CT_BOOL);
      lexer_next(&p->L);
      return;

    case TOK_NIL:
      emit_op(p, OP_NIL);
      comp_set_expr_ctype(CT_NIL_LIT);
      lexer_next(&p->L);
      return;

    case TOK_LPAREN:
      lexer_next(&p->L);
      parse_primary(p);
      parse_infix_loop(p, PREC_EQUALITY);
      if (p->err) return;
      if (p->L.tok != TOK_RPAREN) {
        parser_fail(p, SE_BAD_OPCODE, ERR_EXPECTED_RPAREN);
        return;
      }
      lexer_next(&p->L);
      return;

    case TOK_MINUS:
      lexer_next(&p->L);
      parse_primary(p);
      parse_infix_loop(p, PREC_UNARY);
      if (p->err) return;
      emit_op(p, OP_NEG);
      return;

    case TOK_BANG:
      lexer_next(&p->L);
      parse_primary(p);
      parse_infix_loop(p, PREC_UNARY);
      if (p->err) return;
      emit_op(p, OP_NOT);
      comp_set_expr_ctype(CT_BOOL);
      return;

    case TOK_IDENT: {
      uint16_t name_pos = p->L.tok_pos;
      uint16_t name_len = p->L.tok_len;
      int16_t lidx;
      int16_t fidx;
      /* Locals take precedence inside a function body. */
      if (p->in_function) {
        lidx = locals_find(p->L.src + name_pos, name_len);
        if (lidx >= 0) {
          emit_op_u8(p, OP_GET_LOCAL, (unsigned char)lidx);
          comp_set_expr_ctype(locals_get_ctype((uint8_t)lidx));
          lexer_next(&p->L);
          return;
        }
      }
      gidx = globals_find(p->L.src + name_pos, name_len);
      if (gidx >= 0) {
        emit_op_u8(p, OP_GET_GLOBAL, (unsigned char)gidx);
        comp_set_expr_ctype(globals_get_ctype((uint8_t)gidx));
        lexer_next(&p->L);
        return;
      }
      /* Function call in expression context: f(args). The function's
       * fn_idx is looked up here; we consume the identifier and then
       * dispatch to the call-arglist helper which consumes `(args)`
       * and emits OP_CALL. */
      fidx = funcs_find(p->L.src + name_pos, name_len);
      if (fidx >= 0) {
        lexer_next(&p->L);
        if (p->L.tok != TOK_LPAREN) {
          parser_fail(p, SE_BAD_OPCODE,
                      "need '(...)'");
          return;
        }
        parse_call_arglist_emit(p, (uint8_t)fidx);
        comp_set_expr_ctype(funcs_get_ret_ctype((uint8_t)fidx));
        return;
      }
      /* Expression-context builtins (readLine, min, max) live in
       * main CODE — see src/compiler/builtin_calls.c. The helper
       * expects post-IDENT lexer state, so consume the IDENT first
       * (saved name_pos/name_len carry the identity through). */
      lexer_next(&p->L);
      if (try_compile_builtin_call(p, name_pos, name_len)) return;
      /* `name:` in expression context is a call-site argument label, which the
       * cc65 builds don't accept (labels are host-only; use positional args —
       * f(x), not f(label: x)). Naming it beats a confusing "undeclared name"
       * on the label. Gated to builds with RODATA room: the //e Tier-3
       * Compiler (WITH_AUX_COMPILE, where big labeled programs are most likely)
       * + host; the at-64K-wall II+ baseline keeps the terse error. */
#if defined(WITH_AUX_COMPILE) || !defined(__CC65__)
      if (p->L.tok == TOK_COLON) {
        parser_fail(p, SE_BAD_OPCODE, "use positional args, not labels");
        return;
      }
#endif
      parser_fail(p, SE_BAD_OPCODE, ERR_UNDECLARED_NAME);
      return;
    }

    case TOK_STR:
      compile_string_literal(p);
      comp_set_expr_ctype(CT_STRING);
      return;

    case TOK_LBRACKET: {
      /* Array literal `[a, b, c]`. Emits each element then
       * OP_NEW_ARRAY <n>. Empty `[]` is permitted (creates a
       * zero-count array; the typed-annotation rule from design
       * doc 007 is enforced by the type checker). */
      uint16_t n;
      ctype_t elem_ct;
      ctype_t this_ct;
      elem_ct = CT_UNKNOWN;
      lexer_next(&p->L);  /* consume '[' */
      n = 0;
      if (p->L.tok != TOK_RBRACKET) {
        for (;;) {
          parse_expression(p);
          if (p->err) return;
          this_ct = comp_get_expr_ctype();
          if (n == 0) elem_ct = this_ct;
          else if (this_ct != elem_ct) elem_ct = CT_UNKNOWN;
          if (n == 255) {
            parser_fail(p, SE_BAD_OPCODE, "too many elements");
            return;
          }
          ++n;
          if (p->L.tok != TOK_COMMA) break;
          lexer_next(&p->L);
        }
      }
      if (p->L.tok != TOK_RBRACKET) {
        parser_fail(p, SE_BAD_OPCODE, "expected ']'");
        return;
      }
      lexer_next(&p->L);
      emit_op_u8(p, OP_NEW_ARRAY, (unsigned char)n);
      /* `[Int]` if every element matched a real base type; otherwise
       * `[?]` (heterogeneous or empty literal — empty needs an
       * annotation to resolve). */
      if (elem_ct == CT_UNKNOWN || n == 0) {
        comp_set_expr_ctype(CT_ARR_UNKNOWN);
      } else {
        comp_set_expr_ctype((ctype_t)(CT_ARR_BIT | elem_ct));
      }
      return;
    }

    case TOK_ERROR:
      parser_fail(p, SE_BAD_OPCODE, p->L.err);
      return;

    default:
      parser_fail(p, SE_BAD_OPCODE, "expected expression");
      return;
  }
}

/* Force-unwrap postfix: after any primary expression has been
 * compiled, zero-or-more trailing `!` tokens add OP_UNWRAP each.
 * `x!` runtime-errors if x is nil; on a non-nil value it's a no-op.
 *
 * Caveat: this runs at the END of parse_primary's switch, so
 * a prefix operator like `-x!` parses as `(-x)!` rather than Swift's
 * `-(x!)`. For non-optional ints the two are identical (OP_UNWRAP on
 * a non-nil int is a no-op); when the distinction matters, callers
 * should parenthesise. */
/* Match an identifier against an ASCII string of known length without
 * dragging in the case-insensitive span_equals helper from
 * statements.c (Pratt is in LC, statements.c too — but inlining
 * keeps the dependency simple). Returns 1 on exact byte-for-byte
 * match. */
static int ident_eq(const char *src, uint16_t pos, uint16_t len,
                    const char *want) {
  uint16_t i;
  for (i = 0; i < len; ++i) {
    if (want[i] == 0) return 0;
    if (src[pos + i] != want[i]) return 0;
  }
  return want[len] == 0;
}

static void apply_postfix(Parser *p) {
  ctype_t pre_ct;
  while (!p->err) {
    if (p->L.tok == TOK_BANG) {
      lexer_next(&p->L);
      /* Force-unwrap: T? -> T. Strip the optional bit from the
       * tracked ctype if present; non-optional operands keep their
       * type (the runtime errors anyway). */
      pre_ct = comp_get_expr_ctype();
      emit_op(p, OP_UNWRAP);
      comp_set_expr_ctype((ctype_t)(pre_ct & ~CT_OPT_BIT));
      continue;
    }
    if (p->L.tok == TOK_LBRACKET) {
      /* Subscript read: `arr[i]` → OP_ARR_GET. The write form
       * `arr[i] = v` is handled at statement level (see
       * parse_statement § subscript) because we need to know
       * whether the trailing `=` is present before emitting the
       * load. In expression context only the read is reachable.
       *
       * Type tracking: array's ctype was on the single slot before
       * the index parse overwrote it. Capture pre_ct here so the
       * result type (element of the array) survives the index. */
      pre_ct = comp_get_expr_ctype();
      lexer_next(&p->L);
      parse_expression(p);
      if (p->err) return;
      if (p->L.tok != TOK_RBRACKET) {
        parser_fail(p, SE_BAD_OPCODE, "expected ']'");
        return;
      }
      lexer_next(&p->L);
      emit_op(p, OP_ARR_GET);
      /* If the array's ctype was CT_ARR_<elem>, the subscript yields
       * <elem>. CT_ARR_UNKNOWN -> CT_UNKNOWN. */
      if ((pre_ct & CT_ARR_BIT) != 0) {
        comp_set_expr_ctype((ctype_t)(pre_ct & ~CT_ARR_BIT));
      } else {
        comp_set_expr_ctype(CT_UNKNOWN);
      }
      continue;
    }
    if (p->L.tok == TOK_DOT) {
      uint16_t mname_pos;
      uint16_t mname_len;
      const char *src;
      /* Capture the operand's ctype before the member-name parse
       * overwrites it; `.append` returns the array reference, so
       * the result type stays the array's. */
      pre_ct = comp_get_expr_ctype();
      lexer_next(&p->L);  /* consume `.` */
      if (p->L.tok != TOK_IDENT) {
        parser_fail(p, SE_BAD_OPCODE, ERR_EXPECTED_NAME);
        return;
      }
      mname_pos = p->L.tok_pos;
      mname_len = p->L.tok_len;
      src = p->L.src;
      lexer_next(&p->L);
      if (ident_eq(src, mname_pos, mname_len, "count")) {
        emit_op(p, OP_ARR_LEN);
        comp_set_expr_ctype(CT_INT);
      } else if (ident_eq(src, mname_pos, mname_len, "isEmpty")) {
        /* Desugars to `arr.count == 0`. Cheaper than a new opcode. */
        emit_op(p, OP_ARR_LEN);
        emit_op_u8(p, OP_INT_U8, 0);
        emit_op(p, OP_EQ);
        comp_set_expr_ctype(CT_BOOL);
      } else if (ident_eq(src, mname_pos, mname_len, "append")) {
        /* `.append(v)` mutates the array buffer (possibly
         * reallocating it). OP_ARR_APPEND pushes the (possibly new)
         * array reference as its result, so the user must capture
         * it: `xs = xs.append(5)`. The shorter `xs.append(5)` form
         * compiles but does not update the variable — see
         * docs/using/LANGUAGE.md § Arrays / limitations. The
         * statement-level form `xs.append(v)` (handled in
         * statements.c) gets an implicit write-back; this Pratt
         * postfix path is the expression-context fallback. */
        if (p->L.tok != TOK_LPAREN) {
          parser_fail(p, SE_BAD_OPCODE, ERR_EXPECTED_LPAREN);
          return;
        }
        lexer_next(&p->L);
        parse_expression(p);
        if (p->err) return;
        if (p->L.tok != TOK_RPAREN) {
          parser_fail(p, SE_BAD_OPCODE, ERR_EXPECTED_RPAREN);
          return;
        }
        lexer_next(&p->L);
        emit_op(p, OP_ARR_APPEND);
        /* append's result is the (possibly relocated) array; the
         * element type is preserved. */
        comp_set_expr_ctype(pre_ct);
#if defined(WITH_SWIFTSAT) || defined(WITH_SWIFTAUX) || defined(WITH_SWB) || \
    !defined(__CC65__)
      } else if (try_compile_array_method(p, mname_pos, mname_len, pre_ct)) {
        /* Array methods (removeLast/removeAll/contains) on
         * SWIFTSAT + SWIFTAUX + Family B. The recognition + parse + emit lives in
         * MAIN (builtin_calls.c) so pratt.c's LC object stays small
         * enough for the tight LC arena; here we only check for a
         * syntax error the helper may have flagged. */
        /* Array methods + the Family B string methods hasPrefix/hasSuffix
         * (folded into try_compile_array_method, gated there). */
        if (p->err) return;
#endif
      } else {
        parser_fail(p, SE_BAD_OPCODE, "unknown member");
        return;
      }
      continue;
    }
    break;
  }
}

static void parse_infix_loop(Parser *p, prec_t min_prec) {
  tok_t op;
  prec_t prec;
  unsigned char opcode;

  for (;;) {
    if (p->err) return;
    op = p->L.tok;
    prec = infix_prec(op);
    if (prec < min_prec || prec == PREC_NONE) return;

    lexer_next(&p->L);

    if (op == TOK_QQ) {
      /* `lhs ?? rhs`: emit OP_NIL_COALESCE before evaluating rhs so
       * the VM can short-circuit when lhs is non-nil. Layout:
       *
       *   <lhs>                        ; already on TOS
       *   OP_NIL_COALESCE <skip>       ; if non-nil, jump over rhs
       *   <rhs>                        ; rhs evaluation
       *   <skip>:                      ; convergence
       *
       * Left-associative: `a ?? b ?? c` parses as `(a ?? b) ?? c`,
       * which yields the same value as Swift's right-associative
       * grouping for the binary case (only the order of fallback
       * evaluation differs). */
      uint16_t skip_patch;
      skip_patch = emit_jump_placeholder(p, OP_NIL_COALESCE);
      parse_primary(p);
      apply_postfix(p);
      parse_infix_loop(p, (prec_t)(prec + 1));
      if (p->err) return;
      patch_jump_to_here(p, skip_patch);
      continue;
    }

    parse_primary(p);
    apply_postfix(p);
    /* Left-associative: keep recursing only for STRICTLY higher prec. */
    parse_infix_loop(p, (prec_t)(prec + 1));
    if (p->err) return;
    opcode = opcode_for_binop(op);
    emit_op(p, opcode);
    /* Type tracking: comparisons collapse to Bool; arithmetic ops
     * leave s_expr_ctype at the RHS's type, which equals the LHS's
     * for same-type valid code (Int+Int=Int, String+String=String).
     * Cross-type ops would mis-track here but are type errors
     * commit 5 will catch upstream. */
    if (opcode == OP_EQ || opcode == OP_NEQ ||
        opcode == OP_LT || opcode == OP_LE ||
        opcode == OP_GT || opcode == OP_GE) {
      comp_set_expr_ctype(CT_BOOL);
    }
  }
}

void parse_expression(Parser *p) {
  parse_primary(p);
  apply_postfix(p);
  parse_infix_loop(p, PREC_EQUALITY);
}

/* Used by statements.c after it has already emitted the primary
 * expression for an identifier (because it had to advance past the IDENT
 * to check for `=`). Picks up postfix `!` then infix operators from
 * the current lexer position. */
void parse_infix_continuation(Parser *p) {
  apply_postfix(p);
  parse_infix_loop(p, PREC_EQUALITY);
}

#ifdef __CC65__
#pragma code-name (pop)
#endif
