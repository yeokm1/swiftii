/* Statement-level parsing.
 *
 * Statements:
 *   var_decl    := ('let' | 'var') IDENT '=' expression
 *   assignment  := IDENT '=' expression
 *   print_stmt  := 'print' '(' expression ')'
 *   expr_stmt   := expression
 *
 * Each top-level statement is terminated by newline, semicolon, or EOF.
 * Multiple separators are allowed; empty statements are skipped.
 *
 * Type annotations on `let`/`var` (`let x: Int = 5`) are accepted but
 * not yet verified — adds the type-check pass.
 */
#ifdef __CC65__
/* Compile-time only; relocated to the language card to keep the VM
 * dispatch loop and runtime hot path in main-RAM CODE. */
#pragma code-name (push, "LC")
#endif

#include "parser.h"

#include <stdint.h>
#include "../common/config.h"
#include "../common/ctype.h"
#include "../lexer/tokens.h"
#include "../vm/opcodes.h"
#include "bcbuf.h"
#include "funcs.h"
#include "globals.h"
#include "locals.h"
#include "loops.h"
#if SWIFTII_FUNC_REDEF
#include "../platform/platform.h"   /* redef notice via platform_write */
#endif

/* Streaming-safe lookahead skip for the else-probe sites; see the
 * definition next to parser_skip_separators. Family A builds alias it
 * to the plain skip so codegen is untouched. */
#ifdef WITH_SWB
static void skip_separators_noslide(Parser *p);
#else
#define skip_separators_noslide parser_skip_separators
#endif

/* Shared error strings (cross-TU dedup). Declared in parser.h. */
const char ERR_EXPECTED_NAME[]   = "want name";
const char ERR_EXPECTED_LPAREN[] = "want '('";
const char ERR_EXPECTED_RPAREN[] = "want ')'";
const char ERR_EXPECTED_COLON[]  = "want ':'";
const char ERR_EXPECTED_RANGE[]  = "want '..<'";
const char ERR_UNDECLARED_NAME[] = "undeclared name";

/* Only referenced in this TU. */
static const char ERR_NAME_OVER_LEN[] = "name >11 chars";

/* Validate the current token as a declarable identifier: it must be a
 * TOK_IDENT and fit the symbol-table width (IDENT_MAX-1 = 11 significant
 * chars). On failure it fails the parse with the right message and returns
 * 0; on success returns 1 with `p->L` still on the name token. Call it at
 * each `let`/`var`/`func`/param/`for`/`if let` name position. The symbol
 * tables also reject an over-length name defensively, but catching it here
 * is what turns the truncation footgun into a clear, dedicated diagnostic. */
static int expect_decl_name(Parser *p) {
  if (p->L.tok != TOK_IDENT) {
    parser_fail(p, SE_BAD_OPCODE, ERR_EXPECTED_NAME);
    return 0;
  }
  if (p->L.tok_len >= (uint16_t)IDENT_MAX) {
    parser_fail(p, SE_BAD_OPCODE, ERR_NAME_OVER_LEN);
    return 0;
  }
  return 1;
}

/* ASCII-case-insensitive: `kw` is the lowercase reference, the input
 * span may be either case. Matches builtin names (e.g. `print`) the
 * same way the lexer matches keywords — required so `PRINT(X)` on a
 * stock //+ keyboard works. See docs/contributing/LESSONS.md 2026-05-16.
 *
 * Exported (non-static) so types.c can reuse the same implementation
 * for base_type_from_span without dragging in a second copy. */
int span_equals(const char *src, uint16_t pos, uint16_t len,
                const char *kw) {
  uint16_t i;
  unsigned char tc;
  for (i = 0; i < len; ++i) {
    tc = (unsigned char)src[pos + i];
    if (tc >= 'A' && tc <= 'Z') tc = (unsigned char)(tc + 32);
    if ((unsigned char)kw[i] != tc) return 0;
  }
  return kw[i] == 0;
}

/* parse_type and base_type_from_span moved to src/compiler/types.c
 * (lives in main CODE) — the recognition code wouldn't fit the
 * statements.c LC ceiling. See parser.h for the declaration. */

static void parse_var_decl(Parser *p, unsigned char is_let) {
  uint16_t name_pos;
  uint16_t name_len;
  int16_t gidx;
  int16_t lidx;
  ctype_t declared_ctype;

  lexer_next(&p->L);  /* consume let/var */

  if (!expect_decl_name(p)) return;
  name_pos = p->L.tok_pos;
  name_len = p->L.tok_len;
  lexer_next(&p->L);

  /* Optional type annotation: ': T', ': T?', or ': [T]'. The
   * declared ctype is recorded in the symbol table; commit 4 will
   * validate the initializer against it. */
  declared_ctype = CT_UNKNOWN;
  if (p->L.tok == TOK_COLON) {
    lexer_next(&p->L);
    declared_ctype = parse_type(p);
    if (p->err) return;
  }

  if (p->L.tok != TOK_ASSIGN) {
    parser_fail(p, SE_BAD_OPCODE,
                "missing '='");
    return;
  }
  lexer_next(&p->L);

  parse_expression(p);
  if (p->err) return;

  /* If annotation present, validate the initializer against it
   * (`let x: Int = "hi"` errors here). If not, infer from the
   * Pratt-tracked expression ctype. Annotation wins as the recorded
   * type (so `let m: Int? = 5` stores Int? rather than Int). */
  declared_ctype = resolve_decl_ctype(p, declared_ctype,
                                      comp_get_expr_ctype());
  if (p->err) return;

  if (p->in_function) {
    /* Inside a function body: `let`/`var` becomes a local. The
     * initializer's result is already on top of the VM stack at the
     * exact slot offset the local will occupy (frame_base + slot_idx),
     * so we don't emit any explicit OP_SET_LOCAL — the value just
     * stays there. Subsequent reads use OP_GET_LOCAL <slot>. This
     * matches clox Chapter 22. */
    lidx = locals_declare(p->L.src + name_pos, name_len, is_let,
                          declared_ctype);
    if (lidx < 0) {
      parser_fail(p, SE_BAD_OPCODE, "too many locals");
      return;
    }
    return;
  }

  gidx = globals_define(p->L.src + name_pos, name_len, is_let,
                        declared_ctype);
  if (gidx < 0) {
    parser_fail(p, SE_BAD_OPCODE, "globals full");
    return;
  }
  emit_op_u8(p, OP_DEFINE_GLOBAL, (unsigned char)gidx);
}

/* `print(arg1, arg2,..., terminator: expr)` — the hardcoded
 * builtin call, extended with the optional `terminator:` keyword argument.
 * The default terminator is "\n"; supplying a terminator expression routes
 * to BUILTIN_PRINT_T which prints the evaluated string instead.
 *
 * The terminator must be the LAST argument and evaluate to a String. */
static void parse_print_call(Parser *p) {
  unsigned char argc;
  unsigned char has_terminator;

  lexer_next(&p->L);  /* consume 'print' identifier */
  if (p->L.tok != TOK_LPAREN) {
    parser_fail(p, SE_BAD_OPCODE, ERR_EXPECTED_LPAREN);
    return;
  }
  lexer_next(&p->L);

  argc = 0;
  has_terminator = 0;
  if (p->L.tok != TOK_RPAREN) {
    for (;;) {
      /* Lookahead for `terminator: <expr>`. The lexer offers no
       * two-token peek, so save/restore the Lexer state. */
      if (p->L.tok == TOK_IDENT) {
        Lexer saved;
        saved = p->L;
        if (span_equals(p->L.src, p->L.tok_pos, p->L.tok_len,
                        "terminator")) {
          lexer_next(&p->L);
          if (p->L.tok == TOK_COLON) {
            lexer_next(&p->L);
            parse_expression(p);
            if (p->err) return;
            if (argc == 255) {
              parser_fail(p, SE_BAD_OPCODE, "too many args");
              return;
            }
            ++argc;
            has_terminator = 1;
            break;  /* terminator must be the last argument */
          } else {
            p->L = saved;
          }
        }
      }
      parse_expression(p);
      if (p->err) return;
      if (argc == 255) {
        parser_fail(p, SE_BAD_OPCODE, "too many args");
        return;
      }
      ++argc;
      if (p->L.tok != TOK_COMMA) break;
      lexer_next(&p->L);
    }
  }
  if (p->L.tok != TOK_RPAREN) {
    parser_fail(p, SE_BAD_OPCODE, ERR_EXPECTED_RPAREN);
    return;
  }
  lexer_next(&p->L);

  emit_byte(p, OP_CALL_BUILTIN);
  emit_byte(p, has_terminator ? BUILTIN_PRINT_T : BUILTIN_PRINT);
  emit_byte(p, argc);
  emit_op(p, OP_POP);   /* discard the nil result */
}

/* End-of-expression-statement hook. File mode discards the value with a
 * plain OP_POP. REPL mode at the top level auto-prints the value
 * (BASIC/Python-style: `1 + 2` yields `3`); bare expressions inside
 * loop/if bodies still pop (parse_block clears repl_print_top so a
 * loop body doesn't spam an auto-print every iteration).
 *
 * A CT_VOID result (a void builtin like `home()`/`poke()`)
 * carries no value worth echoing — printing it would surface the nil
 * placeholder as a spurious "nil" line after the side effect. Suppress
 * the echo in that case (SWIFTSAT/SWIFTAUX/host only — the void platform
 * builtins don't exist on lite, so gating keeps those binaries
 * byte-identical). */
static void emit_expr_stmt_end(Parser *p) {
  if (!p->repl_print_top
#if defined(WITH_SWIFTSAT) || defined(WITH_SWIFTAUX) || !defined(__CC65__)
      || comp_get_expr_ctype() == CT_VOID
#endif
     ) {
    emit_op(p, OP_POP);
    return;
  }
  emit_byte(p, OP_CALL_BUILTIN);
  emit_byte(p, BUILTIN_PRINT);
  emit_byte(p, 1);
  emit_op(p, OP_POP);
}

/* Compiler-feature gate for compound assignment (`+=` …) and the
 * in-function loop-body local-slot fix below. Both add compiler-side code;
 * they ship on ALL binaries as of 2026-06-06, funded on the budget-tight
 * II+ images (SWIFTIIP / SWIFTSAT, at the ProDOS ceiling) by dropping the
 * `:list` meta-command (~600 B, see metacmds.c) and relocating
 * parse_assignment LC->MAIN (below). Kept as a single named switch so a
 * future budget crunch can re-gate to host + //e (set to
 * `!defined(__CC65__) || defined(WITH_IIE)`) without re-threading the
 * call sites. */
#define SWIFTII_EXT_COMPILER 1

/* A compound-assignment token (`+=` `-=` `*=` `/=`) folds an arithmetic
 * op into the store: `x += y` is `x = x + y`. Returns the binary opcode
 * for a compound token, or 0 for plain `=` (TOK_ASSIGN). The caller emits
 * a GET of the current value before the RHS and the returned op after, so
 * the result is on TOS for the SET. On II+ it is a constant 0 so the
 * `if (binop)` arms dead-eliminate and the image stays byte-identical. */
#if SWIFTII_EXT_COMPILER
static unsigned char compound_binop(uint8_t tok) {
  /* The compound tokens TOK_PLUS_EQ..TOK_SLASH_EQ (0x68..0x6B) and the
   * arithmetic opcodes OP_ADD..OP_DIV (0x30..0x33) are each consecutive
   * and in matching order, so the map is one subtraction — far smaller
   * than a four-arm switch in the tight LC segment. */
  if (tok >= TOK_PLUS_EQ && tok <= TOK_SLASH_EQ)
    return (unsigned char)(OP_ADD + (tok - TOK_PLUS_EQ));
  return 0;
}
#else
#  define compound_binop(tok) ((unsigned char)0)
#endif

/* The compound-assignment additions push statements.c past its LC code
 * budget on every cc65 build, while MAIN has room; relocate parse_assignment
 * to MAIN to relieve LC (mirrors parse_block_scoped's MAIN placement). On
 * II+ the MAIN room comes from dropping `:list`. The host has no segments. */
#if defined(__CC65__)
#pragma code-name (pop)
#endif
static void parse_assignment(Parser *p, uint16_t name_pos, uint16_t name_len) {
  int16_t lidx;
  int16_t gidx;
#if SWIFTII_EXT_COMPILER
  unsigned char binop = compound_binop(p->L.tok);  /* 0 for plain `=` */
#endif

  /* Lexical scope: locals shadow globals when inside a function. */
  if (p->in_function) {
    lidx = locals_find(p->L.src + name_pos, name_len);
    if (lidx >= 0) {
      if (locals_is_let((uint8_t)lidx)) {
        parser_fail(p, SE_BAD_OPCODE, "let is const");
        return;
      }
#if SWIFTII_EXT_COMPILER
      if (binop) emit_op_u8(p, OP_GET_LOCAL, (uint8_t)lidx);
#endif
      lexer_next(&p->L);  /* consume the assign op */
      parse_expression(p);
      if (p->err) return;
#if SWIFTII_EXT_COMPILER
      if (binop) emit_op(p, binop);
#endif
      check_and_emit_set(p, locals_get_ctype((uint8_t)lidx),
                         OP_SET_LOCAL, (uint8_t)lidx);
      return;
    }
  }

  gidx = globals_find(p->L.src + name_pos, name_len);
  if (gidx < 0) {
    parser_fail(p, SE_BAD_OPCODE, "undeclared");
    return;
  }
  if (globals_is_let((uint8_t)gidx)) {
    parser_fail(p, SE_BAD_OPCODE, "let is const");
    return;
  }
#if SWIFTII_EXT_COMPILER
  if (binop) emit_op_u8(p, OP_GET_GLOBAL, (uint8_t)gidx);
#endif
  lexer_next(&p->L);  /* consume the assign op */
  parse_expression(p);
  if (p->err) return;
#if SWIFTII_EXT_COMPILER
  if (binop) emit_op(p, binop);
#endif
  check_and_emit_set(p, globals_get_ctype((uint8_t)gidx),
                     OP_SET_GLOBAL, (uint8_t)gidx);
}
#if defined(__CC65__)
#pragma code-name (push, "LC")
#endif

static void parse_statement(Parser *p);
static void parse_block(Parser *p);
static void parse_if(Parser *p);
#if SWIFTII_BIGLANG
static void parse_switch(Parser *p);
#endif

/* `f(arg1, arg2, ...)` — emit OP_CALL for the named function. Caller
 * has already consumed the identifier; we expect '(' as the current
 * token. Argument labels (`name: expr`) are accepted but not
 * validated against the function signature — see
 * docs/using/LANGUAGE.md § Functions.
 *
 * Also used by pratt.c's call-expression rule, hence the `void
 * parse_call_arglist_emit(...)` declaration is exposed here (non-
 * static) and only forward-declared in statements.c's local user. */
void parse_call_arglist_emit(Parser *p, uint8_t fn_idx);
void parse_call_arglist_emit(Parser *p, uint8_t fn_idx) {
  unsigned char argc;
  uint8_t expected;

  if (p->L.tok != TOK_LPAREN) {
    parser_fail(p, SE_BAD_OPCODE, ERR_EXPECTED_LPAREN);
    return;
  }
  lexer_next(&p->L);

  argc = 0;
  if (p->L.tok != TOK_RPAREN) {
    for (;;) {
#ifndef __CC65__
      /* Optional argument label `name:` — accepted, but only when
       * followed by an expression (i.e. label is not a standalone
       * identifier expression). Two-token lookahead via the lexer's
       * `saved` mechanism isn't available; instead, peek the buffered
       * `next_tok`-equivalent by saving and restoring the Lexer
       * state.
       *
       * cc65 target: dropped to fund the nested-loop/if recursion-safe
       * fix (docs/contributing/LESSONS.md 2026-06-03) — the lite II+ binary has no banking
       * escape valve. Labels are pure Swift sugar with zero semantic
       * effect here (never validated), so target callers use positional
       * args (`power(2, 10)` not `power(of: 2, to: 10)`). Host keeps
       * label acceptance for the dev REPL + unit coverage. Re-add on the
       * extras binaries via banking if wanted. */
      if (p->L.tok == TOK_IDENT) {
        Lexer saved;
        saved = p->L;
        lexer_next(&p->L);
        if (p->L.tok == TOK_COLON) {
          /* It is a label. Consume the ':' and parse the expression. */
          lexer_next(&p->L);
        } else {
          /* Not a label — restore and parse as expression. */
          p->L = saved;
        }
      }
#endif
      /* An argument can itself be a call (`chk(square(9), 81)`), so
       * parse_expression may recurse back into this function. Under cc65
       * -Cl locals are static, so the inner call would clobber our running
       * argc (and fn_idx) — park them on the re-entrant LIFO across the
       * recursion, the same fix the nested loops use (LESSONS 2026-06-03).
       * Without this, a user-call nested in another user-call's arg list
       * miscounts and fails as `bad arg count` on target (host auto-locals
       * are unaffected). */
      patch_stack_push(fn_idx);
      patch_stack_push(argc);
      parse_expression(p);
      argc   = (unsigned char)patch_stack_pop();
      fn_idx = (uint8_t)patch_stack_pop();
      if (p->err) return;
      if (argc == 255) {
        parser_fail(p, SE_BAD_OPCODE, "too many args");
        return;
      }
#ifndef __CC65__
      /* Per-arg type check against the parameter's declared ctype.
       * Untracked parameters (idx >= FUNC_MAX_TRACKED_PARAMS, or no
       * annotation) return CT_UNKNOWN, which unifies with anything.
       * Host-only (see the label note above) — a compile-time nicety,
       * not a capability; the arg still binds positionally on target. */
      check_type_match(p, funcs_get_param_ctype(fn_idx, argc));
      if (p->err) return;
#endif
      ++argc;
      if (p->L.tok != TOK_COMMA) break;
      lexer_next(&p->L);
    }
  }
  if (p->L.tok != TOK_RPAREN) {
    parser_fail(p, SE_BAD_OPCODE, ERR_EXPECTED_RPAREN);
    return;
  }
  lexer_next(&p->L);

  expected = funcs_get_param_count(fn_idx);
  if (expected != argc) {
    parser_fail(p, SE_BAD_OPCODE, "bad arg count");
    return;
  }

  emit_byte(p, OP_CALL);
  emit_byte(p, fn_idx);
  emit_byte(p, argc);
}

/* `break` — exit the innermost enclosing `while` or `for-in` loop.
 *
 * Compiles to OP_JUMP with a placeholder; the placeholder is
 * registered with the loop-context stack (see loops.c) and patched
 * to the loop's exit cleanup point when the loop body closes. This
 * is the design from docs/contributing/design/004-demo-oriented-scope.md § break:
 * a single unconditional jump, no new VM mechanics.
 *
 * `break` outside any loop is a compile error. `continue` is
 * deliberately deferred (see design doc 004). */
/* Lives in main CODE, not LC: moving this cold helper out of the LC arena
 * reclaims the bytes the if/else-if reentrancy fix added there, keeping
 * the tight SWIFTSAT/SWIFTAUX LC windows within budget (same technique as
 * parse_block_scoped above). */
#ifdef __CC65__
#pragma code-name (pop)
#endif
static void parse_break(Parser *p) {
  uint16_t placeholder_pos;

  lexer_next(&p->L);  /* consume 'break' */
  if (!loops_in_loop()) {
    parser_fail(p, SE_BAD_OPCODE, "break outside");
    return;
  }
  placeholder_pos = emit_jump_placeholder(p, OP_JUMP);
  if (loops_record_break_site(placeholder_pos) != 0) {
    parser_fail(p, SE_BAD_OPCODE, "too many breaks");
    return;
  }
}
#ifdef __CC65__
#pragma code-name (push, "LC")
#endif

/* `return expr` (value return) or `return` (void return). Legal only
 * inside a function body. */
static void parse_return(Parser *p) {
  lexer_next(&p->L);  /* consume 'return' */
  if (!p->in_function) {
    parser_fail(p, SE_BAD_OPCODE, "return outside");
    return;
  }
  /* Bare `return` (followed by newline / ';' / '}') is a void return. */
  if (p->L.tok == TOK_NEWLINE || p->L.tok == TOK_SEMI ||
      p->L.tok == TOK_RBRACE || p->L.tok == TOK_EOF) {
    if (p->fn_has_return) {
      parser_fail(p, SE_BAD_OPCODE, "missing return");
      return;
    }
    emit_op(p, OP_RETURN_V);
    return;
  }
  if (!p->fn_has_return) {
    parser_fail(p, SE_BAD_OPCODE,
                "void no value");
    return;
  }
  parse_expression(p);
  if (p->err) return;
  emit_op(p, OP_RETURN);
}

/* `func name(label? param: Type, ...) -> ReturnType { body }`.
 *
 * Compiles the body inline at the current end-of-scratch, then rotates
 * those bytes into the function arena (so the body persists in REPL
 * mode across input lines). Parameters become locals at slots 0..N-1.
 *
 * Argument-label syntax recognized:
 *   - `_ paramName: Type`  — caller omits the label
 *   - `paramName: Type`     — caller uses `paramName:`
 *   - `extLabel paramName: Type` — caller uses `extLabel:` (label
 * bytes are accepted but not stored)
 *
 * Stores only the parameter count and has-return flag; it
 * does not validate argument labels at call sites. See
 * docs/using/LANGUAGE.md § Functions for the documented limitation. */
static void parse_func_decl(Parser *p) {
  uint16_t name_pos;
  uint16_t name_len;
  int16_t fidx;
  uint8_t param_count;
  unsigned char has_return;
  ctype_t ret_ctype;
  uint16_t body_start_pos;
  uint16_t body_size;
  uint16_t arena_start;
  unsigned char saved_in_function;
  unsigned char saved_fn_has_return;
#if SWIFTII_FUNC_REDEF
  unsigned char is_redef = 0;
#endif

  if (p->in_function) {
    parser_fail(p, SE_BAD_OPCODE, "no nested func");
    return;
  }

  lexer_next(&p->L);  /* consume 'func' */
  if (!expect_decl_name(p)) return;
  name_pos = p->L.tok_pos;
  name_len = p->L.tok_len;
  lexer_next(&p->L);

  /* Declare the function now (with no body address yet) so recursive
   * calls inside the body can resolve the name. */
  fidx = funcs_declare(p->L.src + name_pos, name_len);
  if (fidx < 0) {
#if SWIFTII_FUNC_REDEF
    /* funcs_declare rejects both a name collision and a full table. On the
     * //e REPL binaries a collision is a *redefinition*: reuse the existing
     * slot and repoint it at the new body (with a notice on success). A true
     * full-table is still an error. */
    fidx = funcs_find(p->L.src + name_pos, name_len);
    if (fidx >= 0) {
      funcs_begin_replace((uint8_t)fidx);
      is_redef = 1;
    } else {
      parser_fail(p, SE_BAD_OPCODE, "too many funcs");
      return;
    }
#else
    parser_fail(p, SE_BAD_OPCODE, "too many funcs");
    return;
#endif
  }

  if (p->L.tok != TOK_LPAREN) {
    parser_fail(p, SE_BAD_OPCODE, ERR_EXPECTED_LPAREN);
    return;
  }
  lexer_next(&p->L);

  /* Reset the locals table for this body and start parsing parameters
   * as locals at slots 0..N-1. Disallows nested functions, so
   * we don't need to save / restore the outer scope's locals. */
  locals_reset();

  param_count = 0;
  if (p->L.tok != TOK_RPAREN) {
    for (;;) {
      uint16_t pname_pos;
      uint16_t pname_len;
      /* Optional underscore (no-label form) or extended-label. We
       * consume up to one IDENT or `_` before the real parameter
       * name. The recognized forms are:
       *   `_ name: T`   (underscore consumed, name is the local)
       *   `ext name: T` (ext is an external label, ignored in P4)
       *   `name: T`     (parameter name doubles as label)
       *
       * Since `_` is not a token we have today, we accept any leading
       * IDENT and look ahead one more token to decide. */
      if (p->L.tok == TOK_IDENT) {
        Lexer saved;
        saved = p->L;
        lexer_next(&p->L);
        if (p->L.tok == TOK_IDENT) {
          /* Two idents in a row: first was an external label; second
           * is the parameter name. */
        } else {
          /* Single ident — restore so it's the parameter name. */
          p->L = saved;
        }
      } else {
        parser_fail(p, SE_BAD_OPCODE, ERR_EXPECTED_NAME);
        goto fail_in_params;
      }
      if (!expect_decl_name(p)) goto fail_in_params;
      pname_pos = p->L.tok_pos;
      pname_len = p->L.tok_len;
      lexer_next(&p->L);

      if (p->L.tok != TOK_COLON) {
        parser_fail(p, SE_BAD_OPCODE, ERR_EXPECTED_COLON);
        goto fail_in_params;
      }
      lexer_next(&p->L);
      /* Parameter type. parse_type accepts T, T?, and [T]. */
      {
        ctype_t pctype = parse_type(p);
        if (p->err) goto fail_in_params;
        /* Add the parameter as a local at the next slot. MAX_LOCALS
         * (16) is well below 255 so the locals_declare failure is the
         * single binding-saturation point. */
        if (locals_declare(p->L.src + pname_pos, pname_len, 0, pctype) < 0) {
          parser_fail(p, SE_BAD_OPCODE, "too many params");
          goto fail_in_params;
        }
#ifndef __CC65__
        /* Records the param's ctype for the call-site type check, which is
         * host-only on cc65 (see parse_call_arglist_emit). Skipping the
         * store drops the call (and makes funcs_set_param_ctype + its table
         * dead) on target, helping fund the nested-loop/if fix on SWIFTSAT. */
        funcs_set_param_ctype((uint8_t)fidx, param_count, pctype);
#endif
      }
      ++param_count;
      if (p->L.tok != TOK_COMMA) break;
      lexer_next(&p->L);
    }
  }
  if (p->L.tok != TOK_RPAREN) {
    parser_fail(p, SE_BAD_OPCODE, ERR_EXPECTED_RPAREN);
    goto fail_in_params;
  }
  lexer_next(&p->L);

  /* Optional `-> ReturnType`. */
  has_return = 0;
  ret_ctype = CT_VOID;
  if (p->L.tok == TOK_ARROW) {
    lexer_next(&p->L);
    ret_ctype = parse_type(p);
    if (p->err) goto fail_in_params;
    has_return = 1;
  }

  /* Record the signature now so recursive calls inside the body
   * resolve to the right arity and can be checked at the call site. */
  funcs_set_signature((uint8_t)fidx, param_count, has_return, ret_ctype);

  /* Mark the body's emit point: the function body bytes will live at
   * [body_start_pos .. p->bc_pos) once compiled. After rotation they
   * move to the start of the arena. */
  body_start_pos = p->bc_pos;
  saved_in_function = p->in_function;
  saved_fn_has_return = p->fn_has_return;
  p->in_function = 1;
  p->fn_has_return = has_return;

  parse_block(p);

  p->in_function = saved_in_function;
  p->fn_has_return = saved_fn_has_return;
  if (p->err) {
    locals_reset();
    return;
  }

  /* Ensure the body ends in a return. If the last emitted byte isn't
   * OP_RETURN / OP_RETURN_V, append a void return so calling a void
   * function that fell off the end is well-defined (returns nil). For
   * value-returning functions, falling off the end is a compile error
   * — Swift requires every path to return a value, and does
   * not do flow analysis. */
  if (body_start_pos == p->bc_pos ||
      (BC_GET(p, p->bc_pos - 1) != OP_RETURN &&
       BC_GET(p, p->bc_pos - 1) != OP_RETURN_V)) {
    if (has_return) {
      parser_fail(p, SE_BAD_OPCODE,
                  "missing return");
      locals_reset();
      return;
    }
    emit_op(p, OP_RETURN_V);
    if (p->err) { locals_reset(); return; }
  }

  body_size = (uint16_t)(p->bc_pos - body_start_pos);
  arena_start = bcbuf_rotate_func_into_arena(body_start_pos, body_size);
  if (arena_start == (uint16_t)0xFFFF) {
    parser_fail(p, SE_BAD_OPCODE, "arena err");
    locals_reset();
    return;
  }
  /* p->bc_pos stays at the same absolute offset: the rotation moved
   * the function bytes "down" into the arena and shifted any earlier
   * scratch bytes "up" by body_size, but the total span between the
   * old arena_used and p->bc_pos is unchanged in length. The new
   * scratch start moves to bcbuf_arena_used() but the scratch *end*
   * is still where we last emitted. */

  funcs_set_start((uint8_t)fidx, arena_start);
  locals_reset();
#if SWIFTII_FUNC_REDEF
  /* Announce the rebind only now that the new body compiled cleanly — a
   * mid-body failure rolls the slot back, so no "redef" is printed for it.
   * The name span is canonical (//e keyboard delivers lowercase directly). */
  if (is_redef) {
    platform_write_str("redef ");
    platform_write(p->L.src + name_pos, name_len);
    platform_write_str("\n");
  }
#endif
  return;

fail_in_params:
  locals_reset();
}

/* Parse a `{ stmt; stmt; ... }` block. Consumes both braces. Each
 * inner statement is followed by a newline, ';', or the closing '}'.
 *
 * REPL auto-print of bare expressions is suppressed inside blocks: a
 * `n + 1` inside a `while` body would otherwise spam an auto-print
 * every loop iteration. Only top-level bare expressions print. */
static void parse_block(Parser *p) {
  unsigned char saved_repl_print_top;
  if (p->err) return;
  if (p->L.tok != TOK_LBRACE) {
    parser_fail(p, SE_BAD_OPCODE, "want '{'");
    return;
  }
  lexer_next(&p->L);
  saved_repl_print_top = p->repl_print_top;
  p->repl_print_top = 0;
  for (;;) {
    parser_skip_separators(p);
    if (p->err) { p->repl_print_top = saved_repl_print_top; return; }
    if (p->L.tok == TOK_RBRACE) break;
    if (p->L.tok == TOK_EOF) {
      p->repl_print_top = saved_repl_print_top;
      parser_fail(p, SE_BAD_OPCODE, "unexpected EOF");
      return;
    }
    parse_statement(p);
    if (p->err) { p->repl_print_top = saved_repl_print_top; return; }
    /* After each statement, expect a separator or '}'. If the statement
     * was an if/while (ended at '}'), the lexer is on whatever follows
     * the closing brace — typically a newline. */
    if (p->L.tok != TOK_NEWLINE && p->L.tok != TOK_SEMI &&
        p->L.tok != TOK_RBRACE) {
      p->repl_print_top = saved_repl_print_top;
      parser_fail(p, SE_BAD_OPCODE,
                  "want ';' or '}'");
      return;
    }
  }
  p->repl_print_top = saved_repl_print_top;
  lexer_next(&p->L);  /* consume '}' */
}

/* Parse a `{ ... }` block, then pop every local declared inside it
 * (emitting one OP_POP each) and drop those entries from the compile-
 * time table. `base` is the local count before the block opened — pass
 * the if-let binding's slot so the binding itself is popped too. At top
 * level `base` is 0 and no locals are ever declared, so this is a no-op
 * there (one shared path for both contexts keeps the LC code small).
 *
 * Deliberately placed in MAIN CODE (outside this file's LC pragma) — it
 * is the ~10 B that the if-let work pushed the SWIFTSAT LC area
 * over by; sitting in MAIN (which the OP_NEW_ARRAY/OP_ARR_LEN XLC
 * relocation freed) clears the LC overflow. Cross-segment JSR back into
 * the LC-resident parse_block is a normal call. */
#ifdef __CC65__
#pragma code-name (pop)
#endif
static void parse_block_scoped(Parser *p, uint8_t base) {
  parse_block(p);
  if (p->err) return;
  while (locals_count() > base) {
    emit_op(p, OP_POP);
    locals_truncate((uint8_t)(locals_count() - 1));
  }
}

/* Scope a block whose base is "the local count on entry" — the common case for
 * every conditionally-/repeatedly-executed body (loop bodies, each if/else arm,
 * the if-let some-arm, switch case bodies): a `let`/`var` declared inside is
 * popped on this body's own path, so it can't drift the value stack when the
 * body is taken on some iterations and skipped on others (SE_TYPE_MISMATCH;
 * design 020 / LESSONS). Folding the `locals_count()` + EXT gate into one
 * callee (rather than inlining it at every site) keeps the compiler small —
 * the extras REPLs sit at the MAIN ceiling. (if-let passes an explicit lower
 * base to also pop its binding, so it still calls parse_block_scoped directly.)
 * At top level base == count, so the scope is a no-op (byte-identical there). */
static void parse_block_scoped_auto(Parser *p) {
#if SWIFTII_EXT_COMPILER
  parse_block_scoped(p, locals_count());
#else
  parse_block(p);
#endif
}
#ifdef __CC65__
#pragma code-name (push, "LC")
#endif

/* `if let v = optExpr { body } [else { ... }]` — single-binding if-let.
 *
 * Top-level form binds a global:
 *   <optExpr>; OP_IF_LET <else/skip>; OP_DEFINE_GLOBAL v; <body>
 *   [ OP_JUMP <end>; <else>: <else-body> ]; <end/skip>:
 * OP_IF_LET keeps v on TOS when non-nil (consumed by OP_DEFINE_GLOBAL)
 * and pops+jumps when nil.
 *
 * Function-body form binds a local: OP_IF_LET leaves v on the
 * stack at exactly the next slot, so no store opcode is needed; the
 * some-arm pops the binding (and any block locals) via OP_POP at `}`.
 * The nil arm dropped the optional in OP_IF_LET, so both arms converge
 * at the same VM stack depth. The binding is scoped out of the function
 * after the block; at top level it stays visible afterwards as a global
 * (carry-over). */
static void parse_if_let(Parser *p) {
  uint16_t skip_patch;
  uint16_t end_patch;
  uint16_t name_pos;
  uint16_t name_len;
  uint8_t base;
  Lexer saved;

  lexer_next(&p->L);  /* 'let' */
  if (!expect_decl_name(p)) return;
  name_pos = p->L.tok_pos;
  name_len = p->L.tok_len;
  lexer_next(&p->L);
  if (p->L.tok != TOK_ASSIGN) {
    parser_fail(p, SE_BAD_OPCODE, "want '='");
    return;
  }
  lexer_next(&p->L);
  parse_expression(p);
  if (p->err) return;

  skip_patch = emit_jump_placeholder(p, OP_IF_LET);

  base = locals_count();
  if (p->in_function) {
    /* No store: OP_IF_LET already left v on TOS at the next slot. */
    if (locals_declare(p->L.src + name_pos, name_len, /* is_let */ 1,
                       CT_UNKNOWN) < 0) {
      parser_fail(p, SE_BAD_OPCODE, "too many locals");
      return;
    }
  } else {
    /* Reuse the slot if the name already exists (a previous if-let on
     * the same source); fresh names get a new slot. */
    int16_t gidx;
    gidx = globals_find(p->L.src + name_pos, name_len);
    if (gidx < 0) {
      gidx = globals_define(p->L.src + name_pos, name_len, /* is_let */ 1,
                            CT_UNKNOWN);
      if (gidx < 0) {
        parser_fail(p, SE_BAD_OPCODE, "globals full");
        return;
      }
    }
    emit_op_u8(p, OP_DEFINE_GLOBAL, (unsigned char)gidx);
  }
  /* `skip_patch` (the nil-arm landing placeholder) is live across the body;
   * a nested if/while/for-in in the some-arm would clobber the -Cl static.
   * Park it on the LIFO. See docs/contributing/LESSONS.md 2026-06-03. */
  patch_stack_push(skip_patch);
  parse_block_scoped(p, base);
  skip_patch = patch_stack_pop();
  if (p->err) return;

  /* Optional `else` / `else if`. Lookahead past separators; restore the
   * lexer if there's no else so the caller still sees the separator.
   * No-slide: the snapshot must stay valid across the probe (doc 016). */
  saved = p->L;
  skip_separators_noslide(p);
  if (p->err) return;
  if (p->L.tok != TOK_ELSE) {
    p->L = saved;
    patch_jump_to_here(p, skip_patch);   /* nil arm lands here */
    return;
  }
  lexer_next(&p->L);  /* consume 'else' */
  end_patch = emit_jump_placeholder(p, OP_JUMP);  /* some arm jumps over else */
  patch_jump_to_here(p, skip_patch);              /* nil arm lands at else */
  /* `end_patch` is live across the else arm's recursion — park it too. */
  patch_stack_push(end_patch);
  if (p->L.tok == TOK_IF) {
    parse_if(p);                       /* `else if` — its own scope */
  } else {
    parse_block_scoped_auto(p);
  }
  end_patch = patch_stack_pop();
  if (p->err) return;
  patch_jump_to_here(p, end_patch);
}

static void parse_if(Parser *p) {
  uint16_t else_patch;
  Lexer saved;

  lexer_next(&p->L);  /* consume 'if' */
  /* `if let v = ...` form short-circuits to parse_if_let. */
  if (p->L.tok == TOK_LET) {
    parse_if_let(p);
    return;
  }
  parse_expression(p);
  if (p->err) return;
  else_patch = emit_jump_placeholder(p, OP_JUMP_IF_FALSE);
  /* `else_patch` is live across parse_block; under -Cl a nested if/while/
   * for-in in the then-body would clobber this static local. Park it on the
   * re-entrant LIFO and restore after the body. See docs/contributing/LESSONS.md 2026-06-03. */
  patch_stack_push(else_patch);
  parse_block_scoped_auto(p);                  /* then-body, locals scoped (see helper) */
  else_patch = patch_stack_pop();
  if (p->err) return;

  /* Lookahead past newlines/';' for an `else`. If not present, restore
   * the lexer so the caller still sees the trailing separator.
   * No-slide: the snapshot must stay valid across the probe (doc 016). */
  saved = p->L;
  skip_separators_noslide(p);
  if (p->err) return;
  if (p->L.tok != TOK_ELSE) {
    p->L = saved;
    patch_jump_to_here(p, else_patch);
    return;
  }
  lexer_next(&p->L);  /* consume 'else' */
  /* The "jump over the else to the chain end" placeholder must survive the
   * recursive parse_if below (for `else if`). cc65 builds with -Cl (static
   * locals), so a plain local would be shared with — and clobbered by —
   * that recursive call, which miscompiled 3+-way if/else-if chains (a
   * 2-way if/else, which doesn't recurse, was fine; e.g. fizzbuzz died
   * right after its first Fizz on target). Park it on a re-entrant LIFO in
   * the single static Parser instead — each recursion level keeps its own
   * slot. (Host/clang was unaffected; its locals are real stack slots.) */
  patch_stack_push(emit_jump_placeholder(p, OP_JUMP));
  patch_jump_to_here(p, else_patch);
  if (p->L.tok == TOK_IF) {
    parse_if(p);
  } else {
    parse_block_scoped_auto(p);                /* else-body, scoped like the then-branch */
  }
  if (p->err) return;
  patch_jump_to_here(p, patch_stack_pop());
}

static void parse_while(Parser *p) {
  uint16_t loop_start;
  uint16_t exit_patch;

  lexer_next(&p->L);  /* consume 'while' */
  loop_start = p->bc_pos;
  parse_expression(p);
  if (p->err) return;
  exit_patch = emit_jump_placeholder(p, OP_JUMP_IF_FALSE);
  if (loops_enter() != 0) {
    parser_fail(p, SE_BAD_OPCODE, "loops too deep");
    return;
  }
  /* `loop_start` + `exit_patch` are live across parse_block; under -Cl a
   * nested loop/if in the body clobbers these static locals (the outer loop
   * then ran once). Park them on the re-entrant LIFO. See LESSONS 2026-06-03. */
  patch_stack_push(loop_start);
  patch_stack_push(exit_patch);
  /* Scope body locals: a `var` declared in the loop body pushes a value
   * every iteration, so they must be popped before the back-jump or the
   * VM stack grows without bound and later slot offsets corrupt. (At top
   * level a loop-body `var` is a global, so base == count and this is a
   * no-op — byte-identical there.) II+ keeps the unscoped path (budget). */
  parse_block_scoped_auto(p);
  exit_patch = patch_stack_pop();
  loop_start = patch_stack_pop();
  if (p->err) { loops_exit(p); return; }
  emit_loop(p, loop_start);
  patch_jump_to_here(p, exit_patch);
  /* `break` sites jump to the byte right after the conditional-exit
   * patch. We emit nothing else between the LOOP back-jump and the
   * patch target, so loops_exit (called now) patches break sites to
   * the same location the natural exit lands at. */
  loops_exit(p);
}

#if SWIFTII_BIGLANG
/* `for v in <array> { body }` — element walk. Phase 16 stretch form;
 * gated out of the at-ceiling Family A REPL binaries.
 * The array expression is already parsed and on TOS; `gidx` is the loop
 * variable's global slot (resolved by parse_for_in). Keeps [arr, idx] on
 * the value stack across the loop, with v a global re-bound each iteration
 * (mirrors the range form's global loop-var). Desugars with
 * existing opcodes only (so the Runner needs no new code — see the
 * 2026-06-13 LESSON on why a dedicated OP_ARR_ITER backfired):
 *
 *   <push arr>                       ; [arr]
 *   OP_INT_U8 0                      ; [arr, idx]
 *   loop_start:
 *     OP_OVER OP_ARR_LEN             ; [arr, idx, count]
 *     OP_OVER OP_SWAP OP_LT          ; [arr, idx, idx<count]
 *     OP_JUMP_IF_FALSE exit          ; [arr, idx]
 *     OP_OVER OP_OVER OP_ARR_GET     ; [arr, idx, arr[idx]]
 *     OP_DEFINE_GLOBAL v             ; [arr, idx]
 *     <body>
 *     OP_INC                         ; [arr, idx+1]   (idx is TOS)
 *     OP_LOOP loop_start
 *   exit:
 *   OP_POP OP_POP                    ; discard idx, arr (releases the array) */
static void parse_for_in_array(Parser *p, int16_t gidx) {
  ctype_t arr_ct;
  uint16_t loop_start;
  uint16_t exit_patch;

  /* Type the loop var with the array's element type when known
   * (CT_ARR_<elem> -> <elem>; CT_ARR_UNKNOWN / non-array -> CT_UNKNOWN). */
  arr_ct = comp_get_expr_ctype();
  globals_set_ctype((uint8_t)gidx,
                    (ctype_t)((arr_ct & CT_ARR_BIT)
                                  ? (arr_ct & ~CT_ARR_BIT) : CT_UNKNOWN));

  emit_int_literal(p, 0);   /* idx = 0 ; stack [arr, idx] */

  /* Two values (arr, idx) persist on the value stack across the body;
   * inside a function reserve two anonymous local slots so a body `var`
   * is numbered past them (mirrors the range form's single range_end
   * slot; see that note). At top level body vars are globals, so this is
   * function-only. */
  if (p->in_function) {
    if (locals_declare(p->L.src, 0, 0, CT_UNKNOWN) < 0 ||
        locals_declare(p->L.src, 0, 0, CT_INT) < 0) {
      parser_fail(p, SE_BAD_OPCODE, "too many locals");
      return;
    }
  }

  loop_start = p->bc_pos;
  emit_op(p, OP_OVER);      /* [arr, idx, arr]   */
  emit_op(p, OP_ARR_LEN);   /* [arr, idx, count] */
  emit_op(p, OP_OVER);      /* [arr, idx, count, idx] */
  emit_op(p, OP_SWAP);      /* [arr, idx, idx, count] */
  emit_op(p, OP_LT);        /* [arr, idx, idx<count]  */
  exit_patch = emit_jump_placeholder(p, OP_JUMP_IF_FALSE);

  emit_op(p, OP_OVER);      /* [arr, idx, arr]      */
  emit_op(p, OP_OVER);      /* [arr, idx, arr, idx] */
  emit_op(p, OP_ARR_GET);   /* [arr, idx, arr[idx]] */
  emit_op_u8(p, OP_DEFINE_GLOBAL, (unsigned char)gidx);  /* [arr, idx] */

  if (loops_enter() != 0) {
    parser_fail(p, SE_BAD_OPCODE, "loops too deep");
    return;
  }
  /* loop_start + exit_patch are live across parse_block — park them on the
   * re-entrant LIFO (see parse_while / docs/contributing/LESSONS.md 2026-06-03). gidx is not
   * read after the body (the increment acts on idx on the stack). */
  patch_stack_push(loop_start);
  patch_stack_push(exit_patch);
  parse_block_scoped_auto(p);
  exit_patch = patch_stack_pop();
  loop_start = patch_stack_pop();
  if (p->err) { loops_exit(p); return; }

  emit_op(p, OP_INC);       /* idx is TOS -> idx+1 */
  emit_loop(p, loop_start);

  patch_jump_to_here(p, exit_patch);
  /* break sites land here (loops_exit patches them), before the cleanup
   * POPs, so a `break` also discards [arr, idx]. Two OP_POPs (OP_POP_N is
   * declared but unimplemented in the VM); the second releases the array. */
  loops_exit(p);
  emit_op(p, OP_POP);
  emit_op(p, OP_POP);

  if (p->in_function) locals_truncate((uint8_t)(locals_count() - 2));
}
#endif /* SWIFTII_BIGLANG */

/* `for IDENT in <expr> ..< <expr> { body }` (or `...` for closed range).
 *
 * Desugars to roughly:
 *   <push range_end>          ; stays on the value stack throughout
 *   <push range_start>
 *   OP_DEFINE_GLOBAL i
 *   loop_start:
 *     OP_GET_GLOBAL i
 *     OP_OVER                 ; copy range_end to TOS without popping
 *     OP_LT  (or OP_LE)
 *     OP_JUMP_IF_FALSE exit
 *     <body>
 *     OP_GET_GLOBAL i
 *     OP_INC
 *     OP_SET_GLOBAL i
 *     OP_LOOP loop_start
 *   exit:
 *   OP_POP                    ; discard range_end
 *
 * Deviation from Swift: the loop variable is a global, not a
 * scoped local. Nested for-ins that reuse the same name alias each
 * other. Will scope it properly once locals land. If the name
 * already exists as a `let` binding the compiler errors out — a silent
 * rebind would surprise the user. */
static void parse_for_in(Parser *p) {
  uint16_t name_pos;
  uint16_t name_len;
  int16_t  gidx;
  unsigned char closed_range;
  uint16_t loop_start;
  uint16_t exit_patch;

  lexer_next(&p->L);  /* consume 'for' */

  if (!expect_decl_name(p)) return;
  name_pos = p->L.tok_pos;
  name_len = p->L.tok_len;
  lexer_next(&p->L);

  if (p->L.tok != TOK_IN) {
    parser_fail(p, SE_BAD_OPCODE, "want 'in'");
    return;
  }
  lexer_next(&p->L);

  /* Resolve or define the loop variable as a global. A for-in over a
   * range produces an Int counter; for-in over an array produces the
   * element type (commit 3 wires the array case via Pratt). */
  gidx = globals_find(p->L.src + name_pos, name_len);
  if (gidx < 0) {
    gidx = globals_define(p->L.src + name_pos, name_len, 0, CT_INT);
    if (gidx < 0) {
      parser_fail(p, SE_BAD_OPCODE, "globals full");
      return;
    }
  } else if (globals_is_let((uint8_t)gidx)) {
    parser_fail(p, SE_BAD_OPCODE,
                "for-var let");
    return;
  }

  /* Parse range_start (or, in the array form, the whole array expression),
   * then expect ..< or ... , then range_end. */
  parse_expression(p);
  if (p->err) return;

  if (p->L.tok == TOK_DOT_DOT_LT) {
    closed_range = 0;
  } else if (p->L.tok == TOK_DOT_DOT_DOT) {
    closed_range = 1;
  } else {
#if SWIFTII_BIGLANG
    /* No range operator follows — the parsed expression is the array to
     * iterate (stretch form). */
    parse_for_in_array(p, gidx);
    return;
#else
    parser_fail(p, SE_BAD_OPCODE, ERR_EXPECTED_RANGE);
    return;
#endif
  }
  lexer_next(&p->L);

  /* Stack right now: [range_start]. We need [range_end, range_start]
   * so that range_end sits below the loop var on the stack. Emit
   * range_end onto TOS, then swap. */
  parse_expression(p);
  if (p->err) return;
  emit_op(p, OP_SWAP);
  /* Stack: [range_end, range_start]. */

  /* Define/initialize the loop variable with range_start. */
  if (globals_is_let((uint8_t)gidx)) {
    /* (Should be unreachable — we errored earlier.) */
    parser_fail(p, SE_BAD_OPCODE, "for-var let");
    return;
  }
  /* OP_DEFINE_GLOBAL safely overwrites an existing definition (the VM
   * releases the prior value), so it works for both first-time and
   * rebind cases. */
  emit_op_u8(p, OP_DEFINE_GLOBAL, (unsigned char)gidx);
  /* Stack: [range_end]. */

  /* Inside a function, range_end stays on the value stack for the whole
   * loop, occupying the next physical local slot. Reserve an anonymous
   * local (zero-length name, unfindable) so any `var` declared in the
   * body is numbered to its true stack position — otherwise GET/SET_LOCAL
   * for a body local would alias range_end (the loop never runs / returns
   * 0). At top level body vars are globals, so this is function-only.
   * Released after the range_end POP below. (II+: budget-gated out.) */
#if SWIFTII_EXT_COMPILER
  if (p->in_function) {
    if (locals_declare(p->L.src, 0, 0, CT_INT) < 0) {
      parser_fail(p, SE_BAD_OPCODE, "too many locals");
      return;
    }
  }
#endif

  loop_start = p->bc_pos;
  emit_op_u8(p, OP_GET_GLOBAL, (unsigned char)gidx);
  emit_op(p, OP_OVER);
  emit_op(p, closed_range ? OP_LE : OP_LT);
  exit_patch = emit_jump_placeholder(p, OP_JUMP_IF_FALSE);

  if (loops_enter() != 0) {
    parser_fail(p, SE_BAD_OPCODE, "loops too deep");
    return;
  }
  /* loop_start, exit_patch AND gidx are all live across parse_block — park
   * them on the re-entrant LIFO so a nested loop/if can't clobber the -Cl
   * static locals (see parse_while / docs/contributing/LESSONS.md 2026-06-03). gidx is the
   * loop variable's global slot, read below for the post-body increment; a
   * nested for-in over a *different* variable would otherwise leave the
   * outer loop incrementing the inner counter — an infinite loop. */
  patch_stack_push(loop_start);
  patch_stack_push(exit_patch);
  patch_stack_push((uint16_t)gidx);
  /* Scope body locals so a body `var` is popped before the increment +
   * back-jump (see parse_while). The loop variable itself is a global,
   * unaffected. */
  parse_block_scoped_auto(p);
  gidx = (int16_t)patch_stack_pop();
  exit_patch = patch_stack_pop();
  loop_start = patch_stack_pop();
  if (p->err) { loops_exit(p); return; }

  emit_op_u8(p, OP_GET_GLOBAL, (unsigned char)gidx);
  emit_op(p, OP_INC);
  emit_op_u8(p, OP_SET_GLOBAL, (unsigned char)gidx);
  emit_loop(p, loop_start);

  patch_jump_to_here(p, exit_patch);
  /* Patch break sites to the exit-cleanup point (just before
   * OP_POP). `break` should still pop the cached range_end the
   * for-in left on the stack — emit_break_pops is part of the
   * placeholder design, but for a simple "break for-in" we
   * pre-emit a `OP_POP` after the loop and patch break sites here
   * so they fall through to the POP. */
  loops_exit(p);
  emit_op(p, OP_POP);   /* discard the cached range_end */
  /* Release the anonymous range_end slot reserved above (function-only;
   * in_function can't change across the body since funcs don't nest). */
#if SWIFTII_EXT_COMPILER
  if (p->in_function) locals_truncate((uint8_t)(locals_count() - 1));
#endif
}

#if SWIFTII_BIGLANG
/* `switch <expr> { case <v>[, <v>...]: stmts ... [default: stmts] }`
 * Phase 16 stretch; gated out of the at-ceiling Family A
 * REPL binaries. Pure compiler codegen — desugars to a DUP/EQ/JUMP chain with
 * no new opcodes and no VM change, so the Runner needs nothing. Matching uses
 * OP_EQ, the same opcode `==` emits, so semantics match `==` exactly (Int/Bool
 * by value; String by the pool-literal identity `==` already gives — heap
 * strings compare as `==` does). Each case body runs to the next
 * `case`/`default`/`}` (implicit break, Swift-style); `default:`, if present,
 * must be last. Bodies are scoped like loop bodies (case-body locals popped).
 *
 *   <expr>                       ; [v] stays on the stack across the chain
 *   case A, B:                   ; DUP <A> EQ JUMP_IF_TRUE hit
 *                                ; DUP <B> EQ JUMP_IF_FALSE next
 *     hit: POP <body> JUMP end   ; matched -> drop v, run body, skip the rest
 *   next: ...                    ; default: POP <body>
 *   end: (no-match-no-default path POPs v here)
 *
 * Nested `switch` is rejected (the across-body bookkeeping uses static -Cl
 * locals; a guard keeps them non-reentrant — same class as the documented
 * if/while/for -Cl trap, LESSONS 2026-06-03). */
#define SWITCH_MAX_CASES 24
static uint16_t s_switch_end[SWITCH_MAX_CASES];
static unsigned char s_in_switch;

/* True if the current token starts a `case`/`default` label — the case-body
 * terminator and the clause-start check share this (one copy of the two
 * span_equals instead of inlining them at every site). */
static unsigned char is_case_or_default(Parser *p) {
  return (unsigned char)(p->L.tok == TOK_IDENT &&
      (span_equals(p->L.src, p->L.tok_pos, p->L.tok_len, "case") ||
       span_equals(p->L.src, p->L.tok_pos, p->L.tok_len, "default")));
}

static void parse_switch(Parser *p) {
  uint8_t base_locals;
  unsigned char n_end;
  uint16_t skip_patch;
  unsigned char have_skip;
  unsigned char saw_default;
  unsigned char is_default;
  unsigned char body_returned;
  unsigned char saved_repl_print_top;

  if (s_in_switch) { parser_fail(p, SE_BAD_OPCODE, "nested sw"); return; }
  s_in_switch = 1;

  lexer_next(&p->L);            /* consume 'switch' */
  parse_expression(p);          /* switch value -> [v] */
  if (p->err) goto done;
  /* Matching is OP_EQ (same as `==`), which on strings compares identity,
   * not content (a known language limitation — `"a" == "a"` is false), so a
   * String switch would silently always hit `default`. Reject a
   * statically-known String value rather than misbehave; Int/Bool work
   * correctly. (CT_UNKNOWN is allowed — almost always an Int expression.) */
  if (comp_get_expr_ctype() == CT_STRING) {
    parser_fail(p, SE_BAD_OPCODE, "switch Int/Bool");
    goto done;
  }
  if (p->L.tok != TOK_LBRACE) {
    parser_fail(p, SE_BAD_OPCODE, "want '{'");
    goto done;
  }
  lexer_next(&p->L);

  base_locals = locals_count();
  n_end = 0;
  have_skip = 0;
  saw_default = 0;
  saved_repl_print_top = p->repl_print_top;
  p->repl_print_top = 0;

  for (;;) {
    parser_skip_separators(p);
    if (p->err) goto restore;
    if (p->L.tok == TOK_RBRACE) break;
    if (!is_case_or_default(p)) {
      parser_fail(p, SE_BAD_OPCODE, "want case");
      goto restore;
    }
    /* The previous case's no-match jump lands at this next label. */
    if (have_skip) { patch_jump_to_here(p, skip_patch); have_skip = 0; }

    /* is_case_or_default already confirmed one of the two, so a single
     * span_equals distinguishes them. */
    is_default = (unsigned char)!span_equals(p->L.src, p->L.tok_pos,
                                             p->L.tok_len, "case");
    if (!is_default) {
      unsigned char n_hit = 0;
      lexer_next(&p->L);
      for (;;) {                /* comma-separated case values */
        emit_op(p, OP_DUP);
        parse_expression(p);
        if (p->err) goto restore;
        emit_op(p, OP_EQ);
        if (p->L.tok == TOK_COMMA) {
          patch_stack_push(emit_jump_placeholder(p, OP_JUMP_IF_TRUE));
          n_hit++;
          lexer_next(&p->L);
          continue;
        }
        skip_patch = emit_jump_placeholder(p, OP_JUMP_IF_FALSE);
        have_skip = 1;
        break;
      }
      while (n_hit > 0) { patch_jump_to_here(p, patch_stack_pop()); n_hit--; }
      emit_op(p, OP_POP);       /* matched: drop the switch value */
    } else {
      lexer_next(&p->L);
      emit_op(p, OP_POP);       /* drop the switch value */
      saw_default = 1;
    }
    if (p->L.tok != TOK_COLON) {
      parser_fail(p, SE_BAD_OPCODE, ERR_EXPECTED_COLON);
      goto restore;
    }
    lexer_next(&p->L);

    /* Case body: statements until the next case/default/}. */
    for (;;) {
      parser_skip_separators(p);
      if (p->err) goto restore;
      if (p->L.tok == TOK_RBRACE || is_case_or_default(p)) break;
      parse_statement(p);
      if (p->err) goto restore;
      if (p->L.tok != TOK_NEWLINE && p->L.tok != TOK_SEMI &&
          p->L.tok != TOK_RBRACE) {
        parser_fail(p, SE_BAD_OPCODE, "want ';' or '}'");
        goto restore;
      }
    }
    /* If the body already returned, the trailing scope-POPs and the
     * skip-to-end JUMP are dead code (OP_RETURN cleans the whole frame) and
     * would hide the OP_RETURN from parse_func_decl's last-byte return check
     * — so skip emitting them when the body ended in a return (same heuristic
     * the function-end check uses). The compile-time locals_truncate still
     * runs so the slots free up for the next case. */
    body_returned = (unsigned char)(p->bc_pos > 0 &&
                     (BC_GET(p, p->bc_pos - 1) == OP_RETURN ||
                      BC_GET(p, p->bc_pos - 1) == OP_RETURN_V));

    /* Scope out case-body locals (compile-time slot reuse across cases). */
    while (locals_count() > base_locals) {
      if (!body_returned) emit_op(p, OP_POP);
      locals_truncate((uint8_t)(locals_count() - 1));
    }

    if (is_default) {
      /* `default` must be the last clause. */
      parser_skip_separators(p);
      if (p->L.tok != TOK_RBRACE) {
        parser_fail(p, SE_BAD_OPCODE, "default last");
        goto restore;
      }
      break;
    }
    if (!body_returned) {
      if (n_end >= SWITCH_MAX_CASES) {
        parser_fail(p, SE_BAD_OPCODE, "sw too big");
        goto restore;
      }
      s_switch_end[n_end++] = emit_jump_placeholder(p, OP_JUMP);  /* skip to end */
    }
  }

  lexer_next(&p->L);            /* consume '}' */

  /* Fall-through path (no case matched, no default; or empty switch): the
   * switch value is still on the stack — land the last no-match jump here
   * and drop it. With a default, default already popped it. */
  if (!saw_default) {
    if (have_skip) patch_jump_to_here(p, skip_patch);
    emit_op(p, OP_POP);
  }
  while (n_end > 0) patch_jump_to_here(p, s_switch_end[--n_end]);

restore:
  p->repl_print_top = saved_repl_print_top;
done:
  s_in_switch = 0;
}
#endif /* SWIFTII_BIGLANG */

static void parse_statement(Parser *p) {
  uint16_t name_pos;
  uint16_t name_len;

  if (p->err) return;

  switch (p->L.tok) {
    case TOK_LET:
      parse_var_decl(p, 1);
      return;
    case TOK_VAR:
      parse_var_decl(p, 0);
      return;
    case TOK_FUNC:
      parse_func_decl(p);
      return;
    case TOK_RETURN:
      parse_return(p);
      return;
    case TOK_BREAK:
      parse_break(p);
      return;
    case TOK_IF:
      parse_if(p);
      return;
    case TOK_WHILE:
      parse_while(p);
      return;
    case TOK_FOR:
      parse_for_in(p);
      return;
    case TOK_IDENT:
      /* `print(...)` is a builtin call statement. Other identifiers
       * either start an assignment (IDENT '=' expr, or a compound
       * `+= -= *= /=`) or an expression statement that loads the global. */
      if (span_equals(p->L.src, p->L.tok_pos, p->L.tok_len, "print")) {
        parse_print_call(p);
        return;
      }
#if SWIFTII_BIGLANG
      if (span_equals(p->L.src, p->L.tok_pos, p->L.tok_len, "switch")) {
        parse_switch(p);
        return;
      }
#endif
      name_pos = p->L.tok_pos;
      name_len = p->L.tok_len;
      lexer_next(&p->L);
      if (p->L.tok == TOK_ASSIGN
#if SWIFTII_EXT_COMPILER
          || compound_binop(p->L.tok)
#endif
         ) {
        parse_assignment(p, name_pos, name_len);
        return;
      }
      /* Resolve the identifier once; we may need its slot/kind for
       * subscript-set or .append write-back, both of which mutate
       * the variable. */
      {
        int16_t lidx = -1;
        int16_t gidx = -1;
        int16_t fidx;
        unsigned char is_local;
        if (p->in_function) {
          lidx = locals_find(p->L.src + name_pos, name_len);
        }
        if (lidx < 0) {
          gidx = globals_find(p->L.src + name_pos, name_len);
        }
        is_local = (unsigned char)(lidx >= 0);

        /* Statement-level `IDENT[i] = v`: subscript-set.
         * No speculation: emit the load unconditionally — when no `=`
         * follows it's the same byte the expression-statement path
         * below would emit. Multi-level subscript-set (`a[i][j] = v`)
         * is out of scope; the second `[` would parse via the
         * apply_postfix path under parse_infix_continuation and the
         * assignment would error. */
        if (p->L.tok == TOK_LBRACKET && (lidx >= 0 || gidx >= 0)) {
          ctype_t arr_ct;
          if (is_local) {
            emit_op_u8(p, OP_GET_LOCAL,  (unsigned char)lidx);
            arr_ct = locals_get_ctype((uint8_t)lidx);
          } else {
            emit_op_u8(p, OP_GET_GLOBAL, (unsigned char)gidx);
            arr_ct = globals_get_ctype((uint8_t)gidx);
          }
          lexer_next(&p->L);  /* consume `[` */
          parse_expression(p);
          if (p->err) return;
          parser_expect(p, TOK_RBRACKET, "want ']'");
          if (p->err) return;
          if (p->L.tok == TOK_ASSIGN) {
            ctype_t elem_ct;
            if ((is_local && locals_is_let((uint8_t)lidx)) ||
                (!is_local && globals_is_let((uint8_t)gidx))) {
              parser_fail(p, SE_BAD_OPCODE, "let is const");
              return;
            }
            lexer_next(&p->L);  /* consume `=` */
            parse_expression(p);
            if (p->err) return;
            if ((arr_ct & CT_ARR_BIT) != 0) {
              elem_ct = (ctype_t)(arr_ct & ~CT_ARR_BIT);
              check_type_match(p, elem_ct);
              if (p->err) return;
            }
            emit_op(p, OP_ARR_SET);
            return;
          }
          /* Fall through path: bare `xs[i]` expression statement.
           * Emit OP_ARR_GET ourselves (apply_postfix already ran in
           * parse_expression for the index, but its outer postfix
           * pass over the array reference is bypassed because we
           * consumed `[` here). */
          emit_op(p, OP_ARR_GET);
          comp_set_expr_ctype(((arr_ct & CT_ARR_BIT) != 0)
                              ? (ctype_t)(arr_ct & ~CT_ARR_BIT)
                              : CT_UNKNOWN);
          parse_infix_continuation(p);
          if (p->err) return;
          emit_expr_stmt_end(p);
          return;
        }

        /* Statement-level `IDENT.append(v)`: emit the call AND a
         * write-back so the variable tracks any heap reallocation.
         * Other dot-members (`.count`) fall through to Pratt's
         * postfix path via the generic expression-statement code
         * below. The Lexer save/restore is the cheapest way to peek
         * two tokens — Lexer is ~19 bytes and cc65 emits a small
         * memcpy. */
        if (p->L.tok == TOK_DOT && (lidx >= 0 || gidx >= 0)) {
          Lexer saved;
          saved = p->L;
          lexer_next(&p->L);
          if (p->L.tok == TOK_IDENT &&
              span_equals(p->L.src, p->L.tok_pos, p->L.tok_len, "append")) {
            lexer_next(&p->L);
            parser_expect(p, TOK_LPAREN, ERR_EXPECTED_LPAREN);
            if (p->err) return;
            if (is_local) emit_op_u8(p, OP_GET_LOCAL,  (unsigned char)lidx);
            else          emit_op_u8(p, OP_GET_GLOBAL, (unsigned char)gidx);
            parse_expression(p);
            if (p->err) return;
            parser_expect(p, TOK_RPAREN, ERR_EXPECTED_RPAREN);
            if (p->err) return;
            emit_op(p, OP_ARR_APPEND);
            if (is_local) emit_op_u8(p, OP_SET_LOCAL,  (unsigned char)lidx);
            else          emit_op_u8(p, OP_SET_GLOBAL, (unsigned char)gidx);
            return;
          }
          p->L = saved;
        }

        /* Expression statement starting with this identifier. We
         * already advanced past the IDENT to detect the missing
         * `=`, so emit the load manually. */
        if (is_local) {
          emit_op_u8(p, OP_GET_LOCAL, (unsigned char)lidx);
          parse_infix_continuation(p);
          if (p->err) return;
          emit_expr_stmt_end(p);
          return;
        }
        if (gidx >= 0) {
          emit_op_u8(p, OP_GET_GLOBAL, (unsigned char)gidx);
          parse_infix_continuation(p);
          if (p->err) return;
          emit_expr_stmt_end(p);
          return;
        }
        /* Bare function name as a statement (`f(args)` form). */
        fidx = funcs_find(p->L.src + name_pos, name_len);
        if (fidx >= 0 && p->L.tok == TOK_LPAREN) {
          extern void parse_call_arglist_emit(Parser *p, uint8_t fn_idx);
          parse_call_arglist_emit(p, (uint8_t)fidx);
          if (p->err) return;
          emit_expr_stmt_end(p);
          return;
        }
        /* Bare builtin name (`min(a, b)` at statement level — common
         * in the REPL where it bare-echoes the value). */
        if (p->L.tok == TOK_LPAREN &&
            try_compile_builtin_call(p, name_pos, name_len)) {
          if (p->err) return;
          emit_expr_stmt_end(p);
          return;
        }
        parser_fail(p, SE_BAD_OPCODE, ERR_UNDECLARED_NAME);
        return;
      }
    default:
      /* General expression statement (literal, paren, unary). */
      parse_expression(p);
      if (p->err) return;
      emit_expr_stmt_end(p);
      return;
  }
}

void parser_skip_separators(Parser *p) {
  while (p->L.tok == TOK_NEWLINE || p->L.tok == TOK_SEMI) {
#ifdef WITH_SWB
    /* Streaming source (doc 016): the current token is a dead separator,
     * so nothing holds a (pos,len) across a window slide — the one safe
     * point to pull more source from disk. */
    if (p->refill) p->refill(p);
#endif
    lexer_next(&p->L);
  }
}

#ifdef WITH_SWB
/* Lookahead variant for the else-probe sites (parse_if / parse_if_let),
 * which snapshot the Lexer and restore it when no `else` follows: a
 * streaming window slide between snapshot and restore would leave the
 * saved offsets pointing into post-slide content (the bug showed as the
 * parser silently re-reading slid bytes). Suppress the refill hook for
 * the probe; on restore the outer statement loop re-skips the same
 * separators with sliding re-enabled, so no progress is lost. */
static void skip_separators_noslide(Parser *p) {
  void (*hook)(struct parser *p) = p->refill;
  p->refill = 0;
  parser_skip_separators(p);
  p->refill = hook;
}
#endif

void parse_program(Parser *p) {
  /* Prime the lexer with the first token. */
  lexer_next(&p->L);

  for (;;) {
    parser_skip_separators(p);
    if (p->err) return;
    if (p->L.tok == TOK_EOF) break;

    parse_statement(p);
    if (p->err) return;

    /* Expect terminator: newline, semicolon, or EOF. */
    if (p->L.tok != TOK_NEWLINE && p->L.tok != TOK_SEMI &&
        p->L.tok != TOK_EOF) {
      parser_fail(p, SE_BAD_OPCODE, "want ';' or EOF");
      return;
    }
  }

  emit_op(p, OP_HALT);
}

#ifdef __CC65__
#pragma code-name (pop)
#endif
