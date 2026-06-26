/* Type-annotation parser + compile-time expression-type register
 * for the type tracker.
 *
 * Lives in main CODE (no `#pragma code-name (push, "LC")`) because
 * the recognition code wouldn't fit statements.c's LC budget — see
 * docs/contributing/design/009-type-tracker.md § Implementation plan. Called from
 * statements.c (annotation parse) and pratt.c (expression-type
 * register).
 */
#include "parser.h"

#include <stdint.h>
#include "../common/ctype.h"
#include "../common/errors.h"
#include "../lexer/lexer.h"
#include "../lexer/tokens.h"

/* Single-slot type register; see parser.h for the rationale. */
static ctype_t s_expr_ctype = CT_UNKNOWN;

void comp_set_expr_ctype(ctype_t ct) { s_expr_ctype = ct; }
ctype_t comp_get_expr_ctype(void)    { return s_expr_ctype; }

/* Shared error string — see parser.h for the rationale on placing
 * it in main RODATA rather than LC. */
const char ERR_TYPE_MISMATCH[] = "type mismatch";

int ctype_unifies(ctype_t expected, ctype_t actual) {
  if (expected == actual) return 1;
  /* Anything-to-/from-unknown: accept conservatively so legacy paths
   * the tracker can't pin (e.g., binary ops with mixed-type fallback)
   * still compile. Strict checks land at the specific sites that
   * need them. */
  if (expected == CT_UNKNOWN || actual == CT_UNKNOWN) return 1;
  /* T? accepts T or bare nil. */
  if ((expected & CT_OPT_BIT) != 0) {
    if ((expected & ~CT_OPT_BIT) == actual) return 1;
    if (actual == CT_NIL_LIT) return 1;
  }
  /* [T] accepts [] (empty literal stored as CT_ARR_UNKNOWN); and
   * [] accepts [T] symmetrically (empty annotation on a non-empty
   * literal — odd but harmless). */
  if (expected == CT_ARR_UNKNOWN && (actual & CT_ARR_BIT) != 0) return 1;
  if (actual == CT_ARR_UNKNOWN && (expected & CT_ARR_BIT) != 0) return 1;
  return 0;
}

ctype_t resolve_decl_ctype(Parser *p, ctype_t declared, ctype_t inferred) {
  if (declared == CT_UNKNOWN) return inferred;
  if (!ctype_unifies(declared, inferred)) {
    parser_fail(p, SE_BAD_OPCODE, ERR_TYPE_MISMATCH);
    return CT_UNKNOWN;
  }
  return declared;
}

void check_type_match(Parser *p, ctype_t expected) {
  if (!ctype_unifies(expected, comp_get_expr_ctype())) {
    parser_fail(p, SE_BAD_OPCODE, ERR_TYPE_MISMATCH);
  }
}

void check_and_emit_set(Parser *p, ctype_t target, unsigned char op,
                        uint8_t idx) {
  check_type_match(p, target);
  if (p->err) return;
  emit_op_u8(p, op, idx);
}

/* Match an IDENT span against the five built-in base type names.
 * Case-insensitive on the input (the Apple II input layer
 * auto-lowercases, but file-mode source on the host and Apple II
 * may carry uppercase type names like `Int` — `span_equals` already
 * case-folds, and we fold the dispatch byte for matching parity).
 * Pre-dispatches on the (case-folded) first character so the common
 * case calls span_equals at most once. Returns CT_UNKNOWN on no
 * match — the parser still accepts the annotation, but the tracker
 * treats it as opaque. */
static ctype_t base_type_from_span(const char *src, uint16_t pos,
                                   uint16_t len) {
  unsigned char c;
  if (len == 0) return CT_UNKNOWN;
  c = (unsigned char)src[pos];
  if (c >= 'A' && c <= 'Z') c = (unsigned char)(c + 32);
  if (c == 'i' && span_equals(src, pos, len, "int"))    return CT_INT;
  if (c == 'b' && span_equals(src, pos, len, "bool"))   return CT_BOOL;
  if (c == 's' && span_equals(src, pos, len, "string")) return CT_STRING;
  if (c == 'v' && span_equals(src, pos, len, "void"))   return CT_VOID;
  return CT_UNKNOWN;
}

ctype_t parse_type(Parser *p) {
  ctype_t base;
  unsigned char is_array;

  is_array = 0;
  if (p->L.tok == TOK_LBRACKET) {
    is_array = 1;
    lexer_next(&p->L);
  }
  if (p->L.tok != TOK_IDENT) {
    parser_fail(p, SE_BAD_OPCODE, "want type");
    return CT_UNKNOWN;
  }
  base = base_type_from_span(p->L.src, p->L.tok_pos, p->L.tok_len);
  lexer_next(&p->L);
  if (is_array) {
    if (p->L.tok == TOK_QUESTION) {
      /* [T?] not representable in the 1-byte ctype encoding. */
      parser_fail(p, SE_BAD_OPCODE, "bad type");
      return CT_UNKNOWN;
    }
    if (p->L.tok != TOK_RBRACKET) {
      parser_fail(p, SE_BAD_OPCODE, "want ']'");
      return CT_UNKNOWN;
    }
    lexer_next(&p->L);
    if (p->L.tok == TOK_QUESTION) {
      /* [T]? not representable in the 1-byte ctype encoding. */
      parser_fail(p, SE_BAD_OPCODE, "bad type");
      return CT_UNKNOWN;
    }
    if (base == CT_UNKNOWN) return CT_ARR_UNKNOWN;
    return (ctype_t)(CT_ARR_BIT | base);
  }
  if (p->L.tok == TOK_QUESTION) {
    lexer_next(&p->L);
    if (base == CT_UNKNOWN) return CT_UNKNOWN;
    return (ctype_t)(CT_OPT_BIT | base);
  }
  return base;
}
