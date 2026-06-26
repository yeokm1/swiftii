/* Expression-context builtin call parsing.
 *
 * Recognises calls like `readLine()`, `min(a, b)`, `max(a, b)` from
 * inside Pratt's primary parser. Lives in main CODE rather than LC
 * because pratt.c is in LC and these names + parsers together would
 * not fit the LC ceiling; one helper call from pratt keeps the LC
 * cost down to a few bytes.
 *
 * `random(in:)` was tried in this file at the c7 close and pulled
 * out (~100 B over budget on top of min/max), then shipped as a
 * Family B / host Phase 16 stretch feature.
 *
 * The name comparison is case-sensitive on the canonical Swift form.
 * The //+ input layer (design doc 003 rev 3) auto-lowercases — `min`
 * and `max` are lowercase Swift identifiers so no case marker is
 * needed; `readLine` requires the user to type `'readLine` to inject
 * the capital L.
 */
#include "parser.h"

#include <stdint.h>
#include "../common/config.h"
#include "../common/ctype.h"
#include "../common/errors.h"
#include "../lexer/lexer.h"
#include "../lexer/tokens.h"
#include "../vm/opcodes.h"

static int name_eq(const char *src, uint16_t pos, uint16_t len,
                   const char *want) {
  uint16_t i;
  for (i = 0; i < len; ++i) {
    if (want[i] == 0) return 0;
    if (src[pos + i] != want[i]) return 0;
  }
  return want[len] == 0;
}

static void emit_builtin(Parser *p, unsigned char id, unsigned char argc) {
  emit_byte(p, OP_CALL_BUILTIN);
  emit_byte(p, id);
  emit_byte(p, argc);
}

static void parse_int_arg(Parser *p) {
  parse_expression(p);
  if (p->err) return;
  check_type_match(p, CT_INT);
}

#if SWIFTII_RANDOM
/* `random(in: a..<b)` (half-open) or `random(in: a...b)` (closed) -> Int.
 * Phase 16 stretch, Family B / host only. Emits the two
 * range bounds then OP_CALL_BUILTIN with the half-open / closed id; the LCG
 * worker (builtin_random_in) runs in the Runner / host. */
static void parse_random(Parser *p) {
  unsigned char id;
  parser_expect(p, TOK_LPAREN, ERR_EXPECTED_LPAREN);
  if (p->err) return;
  if (p->L.tok != TOK_IN) {            /* the `in:` argument label */
    parser_fail(p, SE_BAD_OPCODE, "want 'in:'");
    return;
  }
  lexer_next(&p->L);
  parser_expect(p, TOK_COLON, ERR_EXPECTED_COLON);
  if (p->err) return;
  parse_int_arg(p);                    /* lower bound */
  if (p->err) return;
  if (p->L.tok == TOK_DOT_DOT_LT) {
    id = BUILTIN_RANDOM_LT;
  } else if (p->L.tok == TOK_DOT_DOT_DOT) {
    id = BUILTIN_RANDOM_LE;
  } else {
    parser_fail(p, SE_BAD_OPCODE, ERR_EXPECTED_RANGE);
    return;
  }
  lexer_next(&p->L);
  parse_int_arg(p);                    /* upper bound */
  if (p->err) return;
  parser_expect(p, TOK_RPAREN, ERR_EXPECTED_RPAREN);
  if (p->err) return;
  emit_builtin(p, id, 2);
  comp_set_expr_ctype(CT_INT);
}
#endif

/* `name(a, b)` with both args required to be Int. Used by min/max. */
static void parse_min_max(Parser *p, unsigned char id) {
  parser_expect(p, TOK_LPAREN, ERR_EXPECTED_LPAREN);
  if (p->err) return;
  parse_int_arg(p);
  if (p->err) return;
  parser_expect(p, TOK_COMMA, ERR_EXPECTED_COLON);
  if (p->err) return;
  parse_int_arg(p);
  if (p->err) return;
  parser_expect(p, TOK_RPAREN, ERR_EXPECTED_RPAREN);
  if (p->err) return;
  emit_builtin(p, id, 2);
  comp_set_expr_ctype(CT_INT);
}

#if defined(WITH_80COL) && defined(WITH_IIE) && !defined(WITH_SWIFTAUX)
/* `name()` zero-arg builtin: consume the empty arg list, emit `id`, set
 * the result type. Only the //e lite binary (SWIFTIIE) compiles this: it
 * shares the LPAREN/RPAREN/emit/ctype tail across readLine + the
 * text()/text80() width switches so each costs just a name_eq + a call,
 * not a re-inlined ~110 B tail — that dedup is what fits text()/text80()
 * into lite's headroom (SWIFTAUX recognises them via the platform table,
 * the other binaries keep readLine inlined and byte-identical). */
static void parse_zero_arg_builtin(Parser *p, unsigned char id, ctype_t ct) {
  parser_expect(p, TOK_LPAREN, ERR_EXPECTED_LPAREN);
  if (p->err) return;
  parser_expect(p, TOK_RPAREN, ERR_EXPECTED_RPAREN);
  if (p->err) return;
  emit_builtin(p, id, 0);
  comp_set_expr_ctype(ct);
}
#endif

#if defined(WITH_SWIFTSAT) || defined(WITH_SWIFTAUX) || defined(WITH_SWB) || \
    !defined(__CC65__)
/* `name(x)` single-arg builtin: one expression of compile-time type
 * `in_ct`, emitting builtin `id` with a result of type `out_ct`.
 * Shared by the XLC builtins asc and chr (extras-only). Kept gated out
 * of lite — abs/sgn use the cheaper parse_int_arg path (already in lite
 * for min/max) so the II+ lite LC segment doesn't gain this function. */
static void parse_unary_builtin(Parser *p, unsigned char id,
                                ctype_t in_ct, ctype_t out_ct) {
  parser_expect(p, TOK_LPAREN, ERR_EXPECTED_LPAREN);
  if (p->err) return;
  parse_expression(p);
  if (p->err) return;
  check_type_match(p, in_ct);
  if (p->err) return;
  parser_expect(p, TOK_RPAREN, ERR_EXPECTED_RPAREN);
  if (p->err) return;
  emit_builtin(p, id, 1);
  comp_set_expr_ctype(out_ct);
}
#endif

#ifdef WITH_SWB
/* Family B file builtins: `name(s1[, s2])` taking `argc` String
 * args and emitting `id` with result type `out_ct`. Only the standalone
 * Compiler (WITH_SWB) recognises these — the Family A interpreters don't,
 * so a program using readFile/writeFile compiles on a Family B disk and
 * errors as an unknown name in the REPL (the deliberate per-feature dialect
 * fork; the Family A LC has no MLI for file I/O anyway). */
static void parse_file_builtin(Parser *p, unsigned char id,
                               unsigned char argc, ctype_t out_ct) {
  unsigned char i;
  parser_expect(p, TOK_LPAREN, ERR_EXPECTED_LPAREN);
  if (p->err) return;
  for (i = 0; i < argc; ++i) {
    if (i > 0) {
      parser_expect(p, TOK_COMMA, ERR_EXPECTED_COLON);
      if (p->err) return;
    }
    parse_expression(p);
    if (p->err) return;
    check_type_match(p, CT_STRING);
    if (p->err) return;
  }
  parser_expect(p, TOK_RPAREN, ERR_EXPECTED_RPAREN);
  if (p->err) return;
  emit_builtin(p, id, argc);
  comp_set_expr_ctype(out_ct);
}

/* Family B file/dir builtins (doc 015 readFile/writeFile + doc 017 CRUD).
 * Every one takes 1-2 String args; the result ctype varies. A table row
 * per builtin keeps the recognizer compact in the standalone Compiler.
 * deleteDirectory is a name alias of deleteFile (same id $28). */
typedef struct {
  const char *name;
  unsigned char id;
  unsigned char argc;
  ctype_t out_ct;
} FileBuiltinSpec;

static const FileBuiltinSpec file_builtins[] = {
  { "readFile",        BUILTIN_READ_FILE,   1, CT_OPT_STRING },
  { "writeFile",       BUILTIN_WRITE_FILE,  2, CT_BOOL       },
  { "appendFile",      BUILTIN_APPEND_FILE, 2, CT_BOOL       },
  { "deleteFile",      BUILTIN_DELETE_FILE, 1, CT_BOOL       },
  { "deleteDirectory", BUILTIN_DELETE_FILE, 1, CT_BOOL       },
  { "renameFile",      BUILTIN_RENAME_FILE, 2, CT_BOOL       },
  { "fileExists",      BUILTIN_FILE_EXISTS, 1, CT_BOOL       },
  { "createDirectory", BUILTIN_CREATE_DIR,  1, CT_BOOL       },
  { "listDirectory",   BUILTIN_LIST_DIR,    1, CT_ARR_STRING }
};

static int try_file_builtin(Parser *p, uint16_t name_pos, uint16_t name_len) {
  const char *src = p->L.src;
  unsigned char k;
  for (k = 0; k < sizeof(file_builtins) / sizeof(file_builtins[0]); ++k) {
    if (!name_eq(src, name_pos, name_len, file_builtins[k].name)) continue;
    parse_file_builtin(p, file_builtins[k].id, file_builtins[k].argc,
                       file_builtins[k].out_ct);
    return 1;
  }
  return 0;
}
#endif

#if defined(WITH_SWIFTSAT) || defined(WITH_SWIFTAUX) || defined(WITH_SWB) || \
    !defined(__CC65__)
/* Platform builtins — table-driven recognition (XLC; SWIFTSAT +
 * SWIFTAUX, the latter via two grouped copy-down overlays since slice 3
 * step 2). Every entry takes 0..2
 * Int args and emits OP_CALL_BUILTIN <id>
 * <argc>; the dispatcher in builtins_xlc.c does the range/semantic
 * checks. A table row per builtin replaces a verbose name_eq + parse
 * branch (~85 B MAIN each), so the platform surface can grow without
 * eating SWIFTSAT's tight MAIN headroom — a new screen/memory builtin
 * is one row + one XLC dispatcher. All args are Int (peek/poke/htab/
 * vtab); the comma uses ERR_EXPECTED_COLON to match min/max/poke. */
typedef struct {
  const char *name;
  unsigned char id;
  unsigned char argc;
  ctype_t out_ct;
} PlatformBuiltinSpec;

static const PlatformBuiltinSpec platform_builtins[] = {
  { "home",  BUILTIN_HOME,  0, CT_VOID },
  { "peek",  BUILTIN_PEEK,  1, CT_INT  },
  { "poke",  BUILTIN_POKE,  2, CT_VOID },
  { "htab",  BUILTIN_HTAB,  1, CT_VOID },
  { "vtab",  BUILTIN_VTAB,  1, CT_VOID },
  { "gr",     BUILTIN_GR,      0, CT_VOID },
  { "grFull", BUILTIN_GR_FULL, 0, CT_VOID },
  { "text",   BUILTIN_TEXT,    0, CT_VOID },
  { "color",  BUILTIN_COLOR,   1, CT_VOID },
  { "plot",   BUILTIN_PLOT,    2, CT_VOID },
  { "hlin",   BUILTIN_HLIN,    3, CT_VOID },
  { "vlin",   BUILTIN_VLIN,    3, CT_VOID },
  { "scrn",   BUILTIN_SCRN,    2, CT_INT  },
#if defined(WITH_SWB) || !defined(__CC65__)
  /* `wait(_ ms: Int)` — a Family B *program* builtin: recognised only by the
   * standalone Compiler (WITH_SWB, II+ + //e) + host, and executed by the
   * Runner. No REPL ships it — a delay is for compiled programs, not an
   * interactive prompt, and the REPLs are at their MAIN ceiling anyway (it
   * must loop the monitor ROM WAIT $FCA8, which can't relocate off MAIN, and
   * SWIFTSAT has 5 B / SWIFTAUX would carry it for little use). At a REPL,
   * pace with a counted loop. The Runner executes it (vm.c lite dispatch),
   * host via the shared core-builtin dispatcher. */
  { "wait",   BUILTIN_WAIT,    1, CT_VOID },
  /* `tone(_ halfPeriod: Int, _ cycles: Int)` — square-wave speaker tone, the
   * same Family-B-only residency as wait(): the Compiler recognises it, the
   * Runner executes it (vm.c lite dispatch via platform_tone), host is a
   * no-op. No REPL ships it (a tone voices a program, not a prompt). */
  { "tone",   BUILTIN_TONE,    2, CT_VOID },
#endif
#if defined(WITH_SWIFTAUX) || defined(WITH_SWB) || defined(WITH_SWIFTSAT) || \
    !defined(__CC65__)
  /* `text80()` — 80-col switch. SWIFTAUX (//e aux 80-col card), SWIFTSAT (II+
   * Saturn + Videx Videoterm), Family B + host. The dispatcher
   * xlc_text80_dispatch calls platform_text80() — the //e firmware on
   * WITH_IIE, the Videoterm on the II+ Videx arm; on a binary built without a
   * WITH_80COL path it degrades to a push-nil no-op, so one shared Family B
   * Compiler can recognise it for any disk. */
  { "text80", BUILTIN_TEXT80,  0, CT_VOID }
#endif
};

static int try_platform_builtin(Parser *p, uint16_t name_pos,
                                uint16_t name_len) {
  const char *src = p->L.src;
  unsigned char k;
  for (k = 0; k < sizeof(platform_builtins) / sizeof(platform_builtins[0]);
       ++k) {
    const PlatformBuiltinSpec *b = &platform_builtins[k];
    unsigned char i;
    if (!name_eq(src, name_pos, name_len, b->name)) continue;
    parser_expect(p, TOK_LPAREN, ERR_EXPECTED_LPAREN);
    if (p->err) return 1;
    for (i = 0; i < b->argc; ++i) {
      if (i > 0) {
        parser_expect(p, TOK_COMMA, ERR_EXPECTED_COLON);
        if (p->err) return 1;
      }
      parse_int_arg(p);
      if (p->err) return 1;
    }
    parser_expect(p, TOK_RPAREN, ERR_EXPECTED_RPAREN);
    if (p->err) return 1;
    emit_builtin(p, b->id, b->argc);
    comp_set_expr_ctype(b->out_ct);
    return 1;
  }
  return 0;
}
#endif

/* Caller has already consumed the IDENT — current token is whatever
 * follows it (typically `(`). Both call sites (pratt's expression
 * primary, statements.c's bare-statement IDENT handler) advance past
 * the IDENT before invoking this helper so the post-IDENT lexer
 * state matches both paths. */
int try_compile_builtin_call(Parser *p, uint16_t name_pos, uint16_t name_len) {
  const char *src = p->L.src;
#if defined(WITH_80COL) && defined(WITH_IIE) && !defined(WITH_SWIFTAUX)
  /* lite //e (SWIFTIIE): text()/text80() toggle 80-column mode and are
   * program-callable like any builtin (the VM lite path handles the ids
   * inline). SWIFTAUX recognises the same names via the platform table, so
   * a Swift program is source-compatible across both //e binaries. These
   * three zero-arg builtins share one parse_zero_arg_builtin call site to
   * stay inside lite's budget (each is just a name match + an id/ctype). */
  if (name_eq(src, name_pos, name_len, "readLine")) {
    parse_zero_arg_builtin(p, BUILTIN_READLINE, CT_OPT_STRING);
    return 1;
  }
  {
    unsigned char tid = 0;
    if (name_eq(src, name_pos, name_len, "text")) tid = BUILTIN_TEXT;
    else if (name_eq(src, name_pos, name_len, "text80")) tid = BUILTIN_TEXT80;
    if (tid != 0) {
      parse_zero_arg_builtin(p, tid, CT_VOID);
      return 1;
    }
  }
#else
  if (name_eq(src, name_pos, name_len, "readLine")) {
    parser_expect(p, TOK_LPAREN, ERR_EXPECTED_LPAREN);
    if (p->err) return 1;
    parser_expect(p, TOK_RPAREN, ERR_EXPECTED_RPAREN);
    if (p->err) return 1;
    emit_builtin(p, BUILTIN_READLINE, 0);
    comp_set_expr_ctype(CT_OPT_STRING);
    return 1;
  }
#endif
  if (name_eq(src, name_pos, name_len, "min")) {
    parse_min_max(p, BUILTIN_MIN);
    return 1;
  }
  if (name_eq(src, name_pos, name_len, "max")) {
    parse_min_max(p, BUILTIN_MAX);
    return 1;
  }
#if defined(WITH_SWB) || !defined(__CC65__)
  /* abs(_:) / sgn(_:) — pure Int -> Int, FAMILY B ONLY (the standalone
   * Compiler + host recognise them; the REPLs don't, so lite/extras stay
   * byte-identical). abs(x) is real Swift; sgn(x) is a BASIC-flavored
   * free function returning -1/0/1. */
  if (name_eq(src, name_pos, name_len, "abs")) {
    parse_unary_builtin(p, BUILTIN_ABS, CT_INT, CT_INT);
    return 1;
  }
  if (name_eq(src, name_pos, name_len, "sgn")) {
    parse_unary_builtin(p, BUILTIN_SGN, CT_INT, CT_INT);
    return 1;
  }
#endif
#if SWIFTII_RANDOM
  if (name_eq(src, name_pos, name_len, "random")) {
    parse_random(p);
    return 1;
  }
#endif
#ifdef WITH_SWB
  /* Family B file/dir I/O (doc 015 readFile/writeFile + doc 017 CRUD).
   * Implemented in the Runner's lite VM dispatch. */
  if (try_file_builtin(p, name_pos, name_len)) return 1;
#endif
#if defined(WITH_SWIFTSAT) || defined(WITH_SWIFTAUX) || defined(WITH_SWB) || \
    !defined(__CC65__)
  /* `asc(_ s: String) -> Int`. The worker
   * lives in src/vm/builtins_xlc.c (XLC segment on SWIFTSAT, an aux
   * copy-down overlay on SWIFTAUX, normal CODE on host). Lite skips
   * this branch — `asc` is undefined there and falls
   * through to the global-lookup error. asc + chr are the
   * proof builtins; the rest of the extras surface follows the same
   * SWIFTSAT/SWIFTAUX/Family-B/host availability gate. */
  if (name_eq(src, name_pos, name_len, "asc")) {
    parse_unary_builtin(p, BUILTIN_ASC, CT_STRING, CT_INT);
    return 1;
  }
  /* `chr(_ n: Int) -> String` — inverse of
   * asc. Same XLC residency + gating as asc. */
  if (name_eq(src, name_pos, name_len, "chr")) {
    parse_unary_builtin(p, BUILTIN_CHR, CT_INT, CT_STRING);
    return 1;
  }
#endif
#if defined(WITH_SWIFTSAT) || defined(WITH_SWIFTAUX) || defined(WITH_SWB) || \
    !defined(__CC65__)
  /* `Int(_ s: String) -> Int?` — the
   * blocker. Worker parses a decimal string (optional +/- sign) to an
   * int16, returning nil on empty/invalid/overflow. Uppercase Swift
   * convention; the //+ input layer requires `'Int(...)`. Result type
   * is the optional CT_OPT_INT so `if let` / `??` / `!` apply. Same
   * XLC residency + lite gating as asc/chr — SWIFTAUX ports it to its
   * own XLCINT copy-down overlay (slice 3 step 1). */
  if (name_eq(src, name_pos, name_len, "Int")) {
    parse_unary_builtin(p, BUILTIN_STR_TO_INT, CT_STRING, CT_OPT_INT);
    return 1;
  }
#endif
#if defined(WITH_SWIFTSAT) || defined(WITH_SWIFTAUX) || defined(WITH_SWB) || \
    !defined(__CC65__)
  /* Platform builtins (home/peek/poke/htab/vtab + screen/GR
   * slices) — table-driven XLC. Absent on lite (gated out), where they
   * fall through to the global-lookup error. All lowercase Swift names —
   * no //+ case marker needed. SWIFTAUX ships these via two grouped
   * copy-down overlays (slice 3 step 2). */
  if (try_platform_builtin(p, name_pos, name_len)) return 1;
#endif
  /* `String(_ n: Int) -> String` reuses OP_STR_INTERP_I's existing
   * Int → heap-string path; no new builtin id is consumed. Uppercase
   * Swift convention; the //+ input layer requires `'String(...)`.
   * (Its inverse `Int(_ s:)` is the XLC branch above — it was pulled
   * on cc65 budget c8a and re-landed here once the opcode
   * relocations freed MAIN.) */
  if (name_eq(src, name_pos, name_len, "String")) {
    parser_expect(p, TOK_LPAREN, ERR_EXPECTED_LPAREN);
    if (p->err) return 1;
    parse_expression(p);
    if (p->err) return 1;
    check_type_match(p, CT_INT);
    if (p->err) return 1;
    parser_expect(p, TOK_RPAREN, ERR_EXPECTED_RPAREN);
    if (p->err) return 1;
    emit_op(p, OP_STR_INTERP_I);
    comp_set_expr_ctype(CT_STRING);
    return 1;
  }
  return 0;
}

#if defined(WITH_SWIFTSAT) || defined(WITH_SWIFTAUX) || defined(WITH_SWB) || \
    !defined(__CC65__)
/* Array methods (SWIFTSAT + SWIFTAUX + host) — see parser.h for
 * the contract. SWIFTAUX ports each to its own copy-down overlay
 * (XLCRML/XLCRMA/XLCCON) in slice 3 step 1.
 * All three emit OP_CALL_BUILTIN with the receiver array already on the
 * stack and mutate/read in place (same heap offset, no write-back). The
 * receiver's element type drives removeLast's result type. */
int try_compile_array_method(Parser *p, uint16_t name_pos, uint16_t name_len,
                             ctype_t pre_ct) {
  const char *src = p->L.src;
  unsigned char id;
  unsigned char argc;
  ctype_t res_ct;
  if (name_eq(src, name_pos, name_len, "removeLast")) {
    id = BUILTIN_ARR_REMOVE_LAST;
    argc = 1;
    res_ct = ((pre_ct & CT_ARR_BIT) != 0)
             ? (ctype_t)(pre_ct & ~CT_ARR_BIT) : CT_UNKNOWN;
  } else if (name_eq(src, name_pos, name_len, "removeAll")) {
    id = BUILTIN_ARR_REMOVE_ALL;
    argc = 1;
    res_ct = CT_UNKNOWN;
  } else if (name_eq(src, name_pos, name_len, "contains")) {
    id = BUILTIN_ARR_CONTAINS;
    argc = 2;
    res_ct = CT_BOOL;
#if defined(WITH_SWB) || !defined(__CC65__)
  /* String methods hasPrefix(t)/hasSuffix(t) -> Bool (Family B + host).
   * Structurally identical to contains (1 arg, Bool result), so they
   * piggy-back on this recognizer's shared LPAREN/arg/RPAREN/emit tail
   * instead of a separate function — ~40 B vs ~300 B in the tight
   * Compiler BSS. Gated WITH_SWB||host so the extras REPLs (SWIFTSAT/
   * SWIFTAUX, no WITH_SWB) compile these rows out and stay byte-identical.
   * The VM type-checks the T_STR receiver + arg at runtime. */
  } else if (name_eq(src, name_pos, name_len, "hasPrefix")) {
    id = BUILTIN_HAS_PREFIX;
    argc = 2;
    res_ct = CT_BOOL;
  } else if (name_eq(src, name_pos, name_len, "hasSuffix")) {
    id = BUILTIN_HAS_SUFFIX;
    argc = 2;
    res_ct = CT_BOOL;
#endif
  } else {
    return 0;
  }
  parser_expect(p, TOK_LPAREN, ERR_EXPECTED_LPAREN);
  if (p->err) return 1;
  if (argc == 2) {
    parse_expression(p);   /* the needle for contains(v) / has*(t) */
    if (p->err) return 1;
  }
  parser_expect(p, TOK_RPAREN, ERR_EXPECTED_RPAREN);
  if (p->err) return 1;
  emit_builtin(p, id, argc);
  comp_set_expr_ctype(res_ct);
  return 1;
}
#endif
