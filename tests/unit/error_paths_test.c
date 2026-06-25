/* Error-path coverage.
 *
 * One assertion per distinct user-visible error message the compiler,
 * lexer and VM can produce. The REPL surfaces a compile failure as
 * `compile error: <cr.err_msg>` and a runtime failure as `runtime
 * error` (see src/repl/repl.c); tests/repl/017_errors.repl checks that
 * surface end-to-end. Here we pin the underlying message string (and the
 * runtime SE_* code) for *every* path, including the resource-limit and
 * lexer messages that a single hand-written REPL line cannot reach.
 *
 * Each trigger snippet below was verified to actually produce the
 * asserted message — see the git history for the exploration harness
 * that mapped them.
 *
 * Deliberately NOT covered, with the reason:
 *   - "expected '..<'" (ERR_EXPECTED_RANGE) — only reachable in the
 *     non-BIGLANG lite build; in the BIGLANG host/Family-B build a bare
 *     expression after `in` is taken as the for-in-array form, which
 *     fails with "expected '{'" instead. (Lite is covered structurally
 *     by the same parser; the host test binary is BIGLANG.)
 *   - "for-var is let" (statements.c) — dead code, guarded by an earlier
 *     "for-var let" check; the source comment marks it unreachable.
 *   - "bad escape" / "unterminated interp" (strings.c) — defensive, but
 *     masked: the lexer rejects the same inputs first with "unknown
 *     string escape sequence" / "unterminated interpolation in string
 *     literal" (asserted below), so compile_string_literal never sees
 *     them.
 *   - "arena err", "compiled bytecode exceeds buffer", "program too big
 *     for memory", "heap full" — out-of-memory / buffer-overflow guards
 *     on the bytecode arena and string heap. The host buffers are large
 *     enough that reaching them needs a multi-megabyte source; they are
 *     size-budget asserts on the target, not language behaviour.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "common/errors.h"
#include "compiler/compiler.h"
#include "compiler/globals.h"
#include "vm/vm.h"

extern unsigned char *bcbuf_data(void);

#define EXPECT(cond, code) do { if (!(cond)) return (code); } while (0)

/* Compile `src` (len bytes) and assert it fails with exactly `want`.
 * Returns 0 on success, or `code` (with a diagnostic on stderr) on
 * mismatch so the caller can report which row failed. */
static int expect_compile_error(const char *src, uint16_t len,
                                const char *want, int code) {
  CompileResult cr;
  globals_reset();
  vm_reset_globals();
  compiler_compile_source(src, len, &cr);
  if (cr.err == SE_OK) {
    fprintf(stderr, "  error-path: expected \"%s\", got NO error\n", want);
    return code;
  }
  if (cr.err_msg == (const char *)0 || strcmp(cr.err_msg, want) != 0) {
    fprintf(stderr, "  error-path: expected \"%s\", got \"%s\"\n",
            want, cr.err_msg ? cr.err_msg : "(null)");
    return code;
  }
  return 0;
}

struct error_case { const char *src; const char *want; };

/* Compile/lexer error messages reachable from a short source. */
int test_error_paths_compile(void) {
  static const struct error_case cases[] = {
    /* --- statement / declaration parser --- */
    { "let = 1\n",                         "expected name" },
    { "func f { }\n",                      "expected '('" },
    { "print(1\n",                         "expected ')'" },
    { "func f(a Int) {}\n",                "expected ':'" },
    { "if 1 < 2 print(1)\n",               "expected '{'" },
    { "func f() {\n",                      "unexpected EOF" },
    { "func f() { print(1) print(2) }\n",  "expected ';' or '}'" },
    { "print(1) print(2)\n",               "expected ';' or EOF" },
    { "var x Int\n",                       "missing '='" },
    { "if let x 5 { }\n",                   "expected '='" },
    { "let q =\n",                         "expected expression" },
    { "var x: 3 = 1\n",                    "expected type" },
    { "let a = [1, 2\n",                   "expected ']'" },
    { "var a: [Int?] = []\n",              "unsupported type" },
    { "for i 0..<5 {}\n",                  "expected 'in'" },
    { "random(0..<5)\n",                   "expected 'in:'" },

    /* --- name resolution / typing --- */
    { "print(zz)\n",                       "undeclared name" },
    { "let twelvecharss = 1\n",            "name longer than 11 chars" },
    { "var w: Int = \"hi\"\n",             "type mismatch" },
    { "let s = \"ok\"\nprint(s.bad)\n",    "unknown member" },
    { "func f(){}\nlet x = f\n",           "need '(...)'" },

    /* --- functions --- */
    { "func g() -> Int { }\n",             "missing return" },
    { "func f() { return 1 }\n",           "void no value" },
    { "func f(){}\nf(1)\n",                "bad arg count" },
    { "min(a: 1, b: 2)\n",                 "use positional args, not labels" },
    { "func f(){ func g(){} }\n",          "no nested func" },

    /* --- control flow --- */
    { "break\n",                           "break outside" },
    { "return\n",                          "return outside" },
    { "let i = 0\nfor i in 0..<3 {}\n",    "for-var let" },

    /* --- switch --- */
    { "switch 1 { default: print(1); case 1: print(2) }\n", "default last" },
    { "switch 1 { case 1: switch 2 { case 2: print(1) } }\n", "nested sw" },
    { "switch \"a\" { case \"a\": print(1) }\n", "switch Int/Bool" },
    { "switch 1 { print(1) }\n",           "want case" },

    /* --- immutability --- */
    { "let s = \"x\"\ns = \"y\"\n",        "let is const" },
    { "let a = [1]\na[0] = 2\n",           "let is const" },

    /* --- string interpolation (compiler half) --- */
    { "print(\"\\()\")\n",                 "empty interpolation" },
    { "print(\"\\(1 2)\")\n",              "bad interp" },

    /* --- lexer (surfaced through parse_primary) --- */
    { "let x = 99999999999\n",             "integer literal out of Int range" },
    { "print(\"\\q\")\n",                  "unknown string escape sequence" },
    { "print(\"\\(1\")\n",                 "nested string literal not supported in Phase 3 interpolation" },
    { "let x = &1\n",                      "bare '&' is not an operator" },
    { "let x = |1\n",                      "bare '|' is not an operator" },
    { "let x = `\n",                       "unexpected character" },
    { "let x = \"abc\n",                   "newline in string literal" },
    { NULL, NULL }
  };
  /* EOF-terminated lexer messages: the trailing-newline cases above would
   * mask these, so they carry an explicit length with no closing token. */
  static const struct error_case eof_cases[] = {
    { "let x = \"abc",  "unterminated string literal" },
    { "let x = \"\\(a+b", "unterminated interpolation in string literal" },
    { NULL, NULL }
  };
  int i, rc;
  for (i = 0; cases[i].src; i++) {
    rc = expect_compile_error(cases[i].src, (uint16_t)strlen(cases[i].src),
                              cases[i].want, i + 1);
    if (rc) return rc;
  }
  for (i = 0; eof_cases[i].src; i++) {
    rc = expect_compile_error(eof_cases[i].src,
                              (uint16_t)strlen(eof_cases[i].src),
                              eof_cases[i].want, 100 + i);
    if (rc) return rc;
  }
  return 0;
}

/* Resource-limit messages: built by generating a pathological source. */
static char s_big[40000];

int test_error_paths_limits(void) {
  int n, i, rc;

  /* too many locals: > 255 locals in one function */
  n = 0;
  n += snprintf(s_big + n, sizeof(s_big) - (size_t)n, "func f() {\n");
  for (i = 0; i < 300; i++)
    n += snprintf(s_big + n, sizeof(s_big) - (size_t)n, "let v%d = %d\n", i, i);
  n += snprintf(s_big + n, sizeof(s_big) - (size_t)n, "}\n");
  rc = expect_compile_error(s_big, (uint16_t)n, "too many locals", 1);
  if (rc) return rc;

  /* globals full: too many top-level bindings */
  n = 0;
  for (i = 0; i < 400; i++)
    n += snprintf(s_big + n, sizeof(s_big) - (size_t)n, "let g%d = %d\n", i, i);
  rc = expect_compile_error(s_big, (uint16_t)n, "globals full", 2);
  if (rc) return rc;

  /* too many funcs */
  n = 0;
  for (i = 0; i < 400; i++)
    n += snprintf(s_big + n, sizeof(s_big) - (size_t)n, "func f%d() {}\n", i);
  rc = expect_compile_error(s_big, (uint16_t)n, "too many funcs", 3);
  if (rc) return rc;

  /* too many params */
  n = 0;
  n += snprintf(s_big + n, sizeof(s_big) - (size_t)n, "func f(");
  for (i = 0; i < 300; i++)
    n += snprintf(s_big + n, sizeof(s_big) - (size_t)n,
                  "%sa%d: Int", i ? ", " : "", i);
  n += snprintf(s_big + n, sizeof(s_big) - (size_t)n, ") {}\n");
  rc = expect_compile_error(s_big, (uint16_t)n, "too many params", 4);
  if (rc) return rc;

  /* too many elements: oversized array literal */
  n = 0;
  n += snprintf(s_big + n, sizeof(s_big) - (size_t)n, "let a = [");
  for (i = 0; i < 400; i++)
    n += snprintf(s_big + n, sizeof(s_big) - (size_t)n, "%s%d", i ? "," : "", i);
  n += snprintf(s_big + n, sizeof(s_big) - (size_t)n, "]\n");
  rc = expect_compile_error(s_big, (uint16_t)n, "too many elements", 5);
  if (rc) return rc;

  /* too many arguments: user-function call with > 255 args */
  n = 0;
  n += snprintf(s_big + n, sizeof(s_big) - (size_t)n, "func f() {}\nf(");
  for (i = 0; i < 300; i++)
    n += snprintf(s_big + n, sizeof(s_big) - (size_t)n, "%s%d", i ? "," : "", i);
  n += snprintf(s_big + n, sizeof(s_big) - (size_t)n, ")\n");
  rc = expect_compile_error(s_big, (uint16_t)n, "too many arguments", 6);
  if (rc) return rc;

  /* too many args: print() with > 255 args (distinct call path) */
  n = 0;
  n += snprintf(s_big + n, sizeof(s_big) - (size_t)n, "print(");
  for (i = 0; i < 300; i++)
    n += snprintf(s_big + n, sizeof(s_big) - (size_t)n, "%s%d", i ? "," : "", i);
  n += snprintf(s_big + n, sizeof(s_big) - (size_t)n, ")\n");
  rc = expect_compile_error(s_big, (uint16_t)n, "too many args", 7);
  if (rc) return rc;

  /* too many breaks in one loop */
  n = 0;
  n += snprintf(s_big + n, sizeof(s_big) - (size_t)n, "while true {\n");
  for (i = 0; i < 300; i++)
    n += snprintf(s_big + n, sizeof(s_big) - (size_t)n, "break\n");
  n += snprintf(s_big + n, sizeof(s_big) - (size_t)n, "}\n");
  rc = expect_compile_error(s_big, (uint16_t)n, "too many breaks", 8);
  if (rc) return rc;

  /* loops too deep */
  n = 0;
  for (i = 0; i < 40; i++)
    n += snprintf(s_big + n, sizeof(s_big) - (size_t)n, "while true {\n");
  for (i = 0; i < 40; i++)
    n += snprintf(s_big + n, sizeof(s_big) - (size_t)n, "}\n");
  rc = expect_compile_error(s_big, (uint16_t)n, "loops too deep", 9);
  if (rc) return rc;

  /* string too long: literal beyond the string scratch buffer */
  n = 0;
  n += snprintf(s_big + n, sizeof(s_big) - (size_t)n, "let s = \"");
  for (i = 0; i < 5000; i++) s_big[n++] = 'x';
  n += snprintf(s_big + n, sizeof(s_big) - (size_t)n, "\"\n");
  rc = expect_compile_error(s_big, (uint16_t)n, "string too long", 10);
  if (rc) return rc;

  /* sw too big: switch with too many cases */
  n = 0;
  n += snprintf(s_big + n, sizeof(s_big) - (size_t)n, "switch 1 {\n");
  for (i = 0; i < 300; i++)
    n += snprintf(s_big + n, sizeof(s_big) - (size_t)n,
                  "case %d: print(%d)\n", i, i);
  n += snprintf(s_big + n, sizeof(s_big) - (size_t)n, "default: print(0)\n}\n");
  rc = expect_compile_error(s_big, (uint16_t)n, "sw too big", 11);
  if (rc) return rc;

  return 0;
}

/* Compile then run `src`, asserting it ends with exactly runtime code
 * `want`. The source must compile cleanly (else -1). */
static int expect_runtime_error(const char *src, swiftii_err_t want, int code) {
  CompileResult cr;
  swiftii_err_t rc;
  globals_reset();
  vm_reset_globals();
  compiler_compile_source(src, (uint16_t)strlen(src), &cr);
  if (cr.err != SE_OK) {
    fprintf(stderr, "  runtime-path: \"%s\" failed to compile: %s\n",
            src, cr.err_msg ? cr.err_msg : "(null)");
    return code;
  }
  rc = vm_run(bcbuf_data(), cr.program_start, cr.bc_len);
  if (rc != want) {
    fprintf(stderr, "  runtime-path: expected SE %d, got %d\n",
            (int)want, (int)rc);
    return code;
  }
  return 0;
}

/* Runtime (VM) error codes. The REPL collapses them all to "runtime
 * error", but each is a distinct VM trap worth pinning. */
int test_error_paths_runtime(void) {
  int rc;
  rc = expect_runtime_error("print(1 / 0)\n", SE_DIV_ZERO, 1);
  if (rc) return rc;
  rc = expect_runtime_error("print(1 % 0)\n", SE_DIV_ZERO, 2);
  if (rc) return rc;
  rc = expect_runtime_error("let o: Int? = nil\nprint(o!)\n", SE_RUNTIME, 3);
  if (rc) return rc;
  rc = expect_runtime_error("let a = [1]\nprint(a[9])\n", SE_RUNTIME, 4);
  if (rc) return rc;
  rc = expect_runtime_error("var a = [1]\na[9] = 2\n", SE_RUNTIME, 5);
  if (rc) return rc;
  rc = expect_runtime_error("var a = [1]\na.removeLast()\na.removeLast()\n",
                            SE_RUNTIME, 6);
  if (rc) return rc;
  rc = expect_runtime_error("func f() -> Int { return f() }\nprint(f())\n",
                            SE_STACK_OVER, 7);
  if (rc) return rc;
  return 0;
}
