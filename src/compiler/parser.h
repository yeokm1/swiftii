/* Internal compiler state.
 *
 * Shared by compiler.c, statements.c, pratt.c, and emit.c. Not part of
 * the public compiler interface — callers use `compiler_compile_source()` from
 * compiler.h.
 *
 * The Parser bundles the lexer, the bytecode-emission cursor, and a
 * static error slot. A single-pass compiler keeps no AST; what one
 * pass-through of the source produces lives entirely in `bc[]`.
 */
#ifndef SWIFTII_PARSER_H
#define SWIFTII_PARSER_H

#include <stdint.h>
#include "../common/errors.h"
#include "../lexer/lexer.h"

typedef struct parser {
  Lexer L;
  unsigned char *bc;
  uint16_t bc_pos;
  uint16_t bc_cap;
  swiftii_err_t err;
  const char *err_msg;
  uint16_t err_line;
  /* Set by `compiler_compile_source_repl`: a bare top-level expression statement
   * emits `print(value)` instead of discarding. File-mode compilation
   * leaves this 0. */
  unsigned char repl_print_top;
  /* Compile context: 0 = top-level, 1 = inside a function body.
   * Set by `parse_func_decl` while compiling the body; statements
   * inside a function emit `OP_GET_LOCAL`/`OP_SET_LOCAL` and use
   * `locals_*` for declarations, and `return` is legal. Outside a
   * function (0) those constructs are compile errors. */
  unsigned char in_function;
  /* 1 if the current function body declares a `-> SomeType` return
   * (so a value must be returned); 0 for void functions. Only valid
   * when `in_function` is set. */
  unsigned char fn_has_return;
#ifdef WITH_SWB
  /* (Doc 016): streaming-source refill hook, called by
   * parser_skip_separators while the current token is a dead separator
   * — the only point where no source (pos,len) is live across a window
   * slide. NULL = whole source resident (REPL, host tests by default).
   * Installed via compiler_set_refill(); ctx is the SrcWin. */
  void (*refill)(struct parser *p);
  void *refill_ctx;
#endif
} Parser;

/* Mark the parser as failed. First failure wins; later calls are no-ops.
 * `err` is a non-zero swiftii_err_t. `msg` is a static string. */
void parser_fail(Parser *p, swiftii_err_t err, const char *msg);

/* Lexer-helper: advance past trivial separators (newlines, semicolons). */
void parser_skip_separators(Parser *p);

/* Consume `expected` or fail with `msg`. */
void parser_expect(Parser *p, tok_t expected, const char *msg);

/* Shared error message constants. Defined in statements.c; referenced
 * from pratt.c and statements.c. Cross-TU dedup — cc65 cannot collapse
 * identical string literals across compilation units, and pratt.c's
 * RODATA lives in LC while statements.c's is in main, so the linker
 * can't fold them either. Putting them behind named externs gives us
 * one copy each. */
extern const char ERR_EXPECTED_NAME[];
extern const char ERR_EXPECTED_LPAREN[];
extern const char ERR_EXPECTED_RPAREN[];
extern const char ERR_EXPECTED_COLON[];
extern const char ERR_EXPECTED_RANGE[];
extern const char ERR_UNDECLARED_NAME[];

/* From pratt.c */
void parse_expression(Parser *p);
void parse_infix_continuation(Parser *p);

/* From strings.c — compile a string literal (TOK_STR currently). Walks
 * the raw token bytes, processes escapes, parses interpolations via a
 * sub-lexer, and emits an OP_STR sequence (with OP_STR_INTERP_I and
 * OP_ADD as needed for concatenation). Consumes the TOK_STR token. */
void compile_string_literal(Parser *p);

/* From statements.c */
void parse_program(Parser *p);

/* ASCII span-vs-lowercase-keyword equality (case-insensitive on the
 * input span; `kw` must already be lowercase). Lives in statements.c
 * and is exported so types.c (parse_type + base_type_from_span) can
 * reuse the same implementation without dragging in a second copy. */
int span_equals(const char *src, uint16_t pos, uint16_t len, const char *kw);

/* From types.c — parses a type annotation. Caller has already
 * consumed the leading `:` or `->`. Returns the parsed ctype on
 * success, CT_UNKNOWN after parser_fail on error. Lives in main
 * CODE rather than LC so the extra ~250 B of recognition code
 * doesn't blow the statements.c LC ceiling. */
#include "../common/ctype.h"
ctype_t parse_type(Parser *p);

/* Compile-time "result type" of the just-parsed expression. Set by
 * the Pratt parselets that emit value-pushing opcodes (literals,
 * GET_GLOBAL/LOCAL, call return, array literal). Read by statements.c
 * after parse_expression returns: used as the binding's ctype when
 * there's no explicit annotation, and as the validator's actual side
 * when an annotation is present.
 *
 * This is a single-slot register, not a stack — only the outermost
 * expression's result survives because inner expressions set it
 * first (their result), then the outer parselet's emit overwrites
 * with its own result type. For binary operators (`a + b`) the
 * result is generally CT_UNKNOWN unless we can track both operand
 * types cheaply; the validator's CT_UNKNOWN-accepts-anything rule
 * keeps that conservative. The win this slot still buys after the
 * REPL-polish removal is in `:list` (`var xs = [1, 2, 3]` shows
 * `xs: [Int]` rather than `[?]`) and in array-literal-vs-annotation
 * type checking. */
void comp_set_expr_ctype(ctype_t ct);
ctype_t comp_get_expr_ctype(void);

/* Type-unification predicate used by the type-unification validator.
 * Returns 1 if a value of type `actual` is acceptable where the
 * compiler expects `expected`, 0 otherwise. The rules (per design
 * doc 009 § Detailed design):
 *
 *   T            ↔ T          : OK (exact match)
 *   T?           ↔ T          : OK (lift to optional)
 *   T?           ↔ nil literal: OK
 *   [T]          ↔ []         : OK (empty literal acquires elem)
 *   T            ↔ T?         : NOT OK (must unwrap with `!` or `??`)
 *   CT_UNKNOWN   ↔ anything   : OK (conservative — preserves the
 *                                   pre-tracker accept-everything
 *                                   behaviour for code paths the
 *                                   tracker can't pin a type on)
 *
 * Note the asymmetry: `let x: Int? = 5` is fine, but
 * `let y: Int = someOpt` is a compile error. */
int ctype_unifies(ctype_t expected, ctype_t actual);

/* Shared "type mismatch" error string. Defined in types.c so LC
 * (statements.c, where the validator hooks live) only pays the
 * extern reference, not the string bytes. */
extern const char ERR_TYPE_MISMATCH[];

/* Resolve the final ctype of a `let`/`var` binding. If `declared`
 * is CT_UNKNOWN (no annotation), returns the inferred Pratt ctype.
 * Otherwise validates inferred unifies with declared, calling
 * parser_fail on mismatch. Returns the resolved ctype (which is
 * `declared` when annotated, `inferred` when not). On error returns
 * CT_UNKNOWN; the caller should bail via p->err. */
ctype_t resolve_decl_ctype(Parser *p, ctype_t declared, ctype_t inferred);

/* Validate then emit a SET_GLOBAL / SET_LOCAL pair. The compile-
 * time type of the binding (`target`) must unify with the
 * Pratt-tracked expression type currently on the type stack. On
 * mismatch, parser_fail and skip the emit. Combines the
 * ctype_unifies + parser_fail + emit_op_u8 pattern that was
 * repeated at two LC call sites in statements.c. */
void check_and_emit_set(Parser *p, ctype_t target, unsigned char op,
                        uint8_t idx);

/* parser_fail with ERR_TYPE_MISMATCH iff the Pratt-tracked expression
 * type does not unify with `expected`. Pulls the ctype_unifies +
 * parser_fail pattern out of LC call sites — both the subscript-set
 * element check and the per-arg call check use this. */
void check_type_match(Parser *p, ctype_t expected);

/* From builtin_calls.c — try to compile `name(...)` as an expression-
 * context builtin (readLine, min, max, random). Returns 1 if `name`
 * matched a known builtin (caller has not consumed the IDENT; this
 * helper consumes it on a match, plus the full argument list). Returns
 * 0 if `name` is not a builtin; the caller falls through. Lives in
 * main CODE so the recognition table doesn't bloat LC. */
int try_compile_builtin_call(Parser *p, uint16_t name_pos, uint16_t name_len);

#if defined(WITH_SWIFTSAT) || defined(WITH_SWIFTAUX) || defined(WITH_SWB) || \
    !defined(__CC65__)
/* Array methods `.removeLast()/.removeAll()/
 * .contains(v)` (SWIFTSAT + SWIFTAUX + Family B + host). Called from the Pratt
 * postfix dot-member handler after
 * the receiver array + member name have been consumed (current token is
 * what follows the name, typically `(`). `pre_ct` is the receiver's
 * compile-time ctype (for removeLast's element-type result). Returns 1
 * if the member name matched one of the three methods (the call is
 * emitted, or p->err is set on a syntax error); 0 if not, so the caller
 * reports "unknown member". Lives in main CODE — not pratt.c's LC
 * object — so SWIFTSAT's tight LC arena doesn't overflow. */
int try_compile_array_method(Parser *p, uint16_t name_pos, uint16_t name_len,
                             ctype_t pre_ct);
/* The Family B string methods hasPrefix(t)/hasSuffix(t) -> Bool are folded
 * into try_compile_array_method (gated WITH_SWB||host there) — they share
 * its 2-arg/Bool parse tail, so no separate entry point is needed. */
#endif

/* From emit.c */
void emit_byte(Parser *p, unsigned char b);
void emit_op(Parser *p, unsigned char op);
void emit_op_u8(Parser *p, unsigned char op, unsigned char operand);
void emit_op_u16(Parser *p, unsigned char op, uint16_t operand);
void emit_int_literal(Parser *p, int16_t value);

/* Emit a forward jump opcode (OP_JUMP / OP_JUMP_IF_FALSE / etc.) with
 * a placeholder offset. Returns the bytecode position of the low byte
 * of the operand, to be passed to `patch_jump_to_here` once the target
 * is known. Two bytes wide. */
uint16_t emit_jump_placeholder(Parser *p, unsigned char op);

/* Fill in a placeholder so it jumps to the current bytecode position.
 * The offset is relative to the byte immediately after the 2-byte
 * operand. */
void patch_jump_to_here(Parser *p, uint16_t placeholder_pos);

/* Emit OP_LOOP with the backward distance to `loop_start` (an earlier
 * bytecode position). The 6502 VM reads u16 unsigned and subtracts. */
void emit_loop(Parser *p, uint16_t loop_start);

/* Re-entrant placeholder LIFO (emit.c): push a bytecode-cursor value that
 * must survive a recursive parse_block, pop it after. Keeps -Cl static
 * locals (else_patch / loop_start / exit_patch / gidx / skip_patch /
 * end_patch / jump-over-else) safe across nested if/while/for-in/if-let.
 * patch_stack_reset() rewinds the cursor at the start of each compile. */
void patch_stack_reset(void);
void patch_stack_push(uint16_t v);
uint16_t patch_stack_pop(void);

#endif /* SWIFTII_PARSER_H */
