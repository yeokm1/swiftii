/* Compiler unit tests.
 *
 * Compile small source snippets, inspect the emitted bytecode, and run
 * it through the VM with captured stdout. The capture uses dup2 the
 * same way vm_test.c does — see that file for the rationale.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "common/errors.h"
#include "compiler/compiler.h"
#include "compiler/globals.h"
#include "vm/vm.h"
#include "vm/opcodes.h"
#include "platform/platform.h"

#define EXPECT(cond, code) do { if (!(cond)) return (code); } while (0)

static int run_and_capture(const char *src, char *out, size_t out_cap) {
  CompileResult cr;
  swiftii_err_t rc;
  int saved_fd, capture_fd;
  ssize_t n;
  const char *tmp_path = "/tmp/swiftii_compiler_test.txt";

  globals_reset();
  vm_reset_globals();

  compiler_compile_source(src, (uint16_t)strlen(src), &cr);
  if (cr.err != SE_OK) return -1;

  fflush(stdout);
  saved_fd = dup(1);
  if (saved_fd < 0) return -2;
  capture_fd = open(tmp_path, O_RDWR | O_CREAT | O_TRUNC, 0600);
  if (capture_fd < 0) { close(saved_fd); return -3; }
  if (dup2(capture_fd, 1) < 0) {
    close(saved_fd);
    close(capture_fd);
    return -4;
  }
  close(capture_fd);

  platform_init();
  {
    extern unsigned char *bcbuf_data(void);
    rc = vm_run(bcbuf_data(), cr.program_start, cr.bc_len);
  }
  platform_shutdown();
  fflush(stdout);

  dup2(saved_fd, 1);
  close(saved_fd);

  if (rc != SE_OK) return -5;

  capture_fd = open(tmp_path, O_RDONLY);
  if (capture_fd < 0) return -6;
  n = read(capture_fd, out, out_cap - 1);
  close(capture_fd);
  if (n < 0) return -7;
  out[n] = '\0';
  return 0;
}

int test_compile_demo_program(void) {
  char buf[64];
  int rc;
  /* The roadmap demo. */
  rc = run_and_capture("let x = 21\nlet y = 2\nprint(x * y)\n",
                       buf, sizeof(buf));
  EXPECT(rc == 0, 1);
  EXPECT(strcmp(buf, "42\n") == 0, 2);
  return 0;
}

int test_compile_arithmetic(void) {
  char buf[64];
  EXPECT(run_and_capture("print(1 + 2 * 3)\n", buf, sizeof(buf)) == 0, 1);
  EXPECT(strcmp(buf, "7\n") == 0, 2);

  EXPECT(run_and_capture("print((1 + 2) * 3)\n", buf, sizeof(buf)) == 0, 3);
  EXPECT(strcmp(buf, "9\n") == 0, 4);

  EXPECT(run_and_capture("print(10 - 3 - 2)\n", buf, sizeof(buf)) == 0, 5);
  EXPECT(strcmp(buf, "5\n") == 0, 6);

  EXPECT(run_and_capture("print(20 / 6)\n", buf, sizeof(buf)) == 0, 7);
  EXPECT(strcmp(buf, "3\n") == 0, 8);

  EXPECT(run_and_capture("print(20 % 6)\n", buf, sizeof(buf)) == 0, 9);
  EXPECT(strcmp(buf, "2\n") == 0, 10);

  EXPECT(run_and_capture("print(-7)\n", buf, sizeof(buf)) == 0, 11);
  EXPECT(strcmp(buf, "-7\n") == 0, 12);
  return 0;
}

int test_compile_comparison(void) {
  char buf[64];
  EXPECT(run_and_capture("print(1 == 1)\n", buf, sizeof(buf)) == 0, 1);
  EXPECT(strcmp(buf, "true\n") == 0, 2);
  EXPECT(run_and_capture("print(1 != 2)\n", buf, sizeof(buf)) == 0, 3);
  EXPECT(strcmp(buf, "true\n") == 0, 4);
  EXPECT(run_and_capture("print(1 < 2)\n", buf, sizeof(buf)) == 0, 5);
  EXPECT(strcmp(buf, "true\n") == 0, 6);
  EXPECT(run_and_capture("print(2 <= 2)\n", buf, sizeof(buf)) == 0, 7);
  EXPECT(strcmp(buf, "true\n") == 0, 8);
  EXPECT(run_and_capture("print(3 > 2)\n", buf, sizeof(buf)) == 0, 9);
  EXPECT(strcmp(buf, "true\n") == 0, 10);
  EXPECT(run_and_capture("print(!false)\n", buf, sizeof(buf)) == 0, 11);
  EXPECT(strcmp(buf, "true\n") == 0, 12);
  return 0;
}

int test_compile_logical_ops(void) {
  char buf[128];
  /* Values. */
  EXPECT(run_and_capture("print(true && true)\n", buf, sizeof(buf)) == 0, 1);
  EXPECT(strcmp(buf, "true\n") == 0, 2);
  EXPECT(run_and_capture("print(true && false)\n", buf, sizeof(buf)) == 0, 3);
  EXPECT(strcmp(buf, "false\n") == 0, 4);
  EXPECT(run_and_capture("print(false || true)\n", buf, sizeof(buf)) == 0, 5);
  EXPECT(strcmp(buf, "true\n") == 0, 6);
  EXPECT(run_and_capture("print(false || false)\n", buf, sizeof(buf)) == 0, 7);
  EXPECT(strcmp(buf, "false\n") == 0, 8);
  /* Precedence: && binds tighter than ||, comparison tighter than both.
   * `false || true && false` == `false || (true && false)` == false. */
  EXPECT(run_and_capture("print(false || true && false)\n",
                         buf, sizeof(buf)) == 0, 9);
  EXPECT(strcmp(buf, "false\n") == 0, 10);
  EXPECT(run_and_capture("print(1 < 2 && 2 < 3)\n", buf, sizeof(buf)) == 0, 11);
  EXPECT(strcmp(buf, "true\n") == 0, 12);
  /* Short-circuit: the rhs side effect must NOT run when the lhs alone
   * decides the result (`false &&`, `true ||`). */
  EXPECT(run_and_capture(
      "func s() -> Bool {\n  print(\"X\")\n  return true\n}\n"
      "print(false && s())\n", buf, sizeof(buf)) == 0, 13);
  EXPECT(strcmp(buf, "false\n") == 0, 14);
  EXPECT(run_and_capture(
      "func s() -> Bool {\n  print(\"X\")\n  return true\n}\n"
      "print(true || s())\n", buf, sizeof(buf)) == 0, 15);
  EXPECT(strcmp(buf, "true\n") == 0, 16);
  /* ...and the rhs DOES run when the lhs leaves the result open. */
  EXPECT(run_and_capture(
      "func s() -> Bool {\n  print(\"X\")\n  return true\n}\n"
      "print(true && s())\n", buf, sizeof(buf)) == 0, 17);
  EXPECT(strcmp(buf, "X\ntrue\n") == 0, 18);
  return 0;
}

int test_compile_var_reassignment(void) {
  char buf[64];
  EXPECT(run_and_capture("var n = 5\nn = n + 1\nprint(n)\n",
                         buf, sizeof(buf)) == 0, 1);
  EXPECT(strcmp(buf, "6\n") == 0, 2);
  return 0;
}

int test_compile_let_is_immutable(void) {
  CompileResult cr;
  globals_reset();
  compiler_compile_source("let x = 1\nx = 2\n", 17, &cr);
  EXPECT(cr.err != SE_OK, 1);
  return 0;
}

int test_compile_undeclared_var_is_error(void) {
  CompileResult cr;
  globals_reset();
  compiler_compile_source("print(missing)\n", 15, &cr);
  EXPECT(cr.err != SE_OK, 1);
  return 0;
}

int test_compile_ident_too_long_is_error(void) {
  /* >11-char identifier is a compile-time error, not silent truncation, and
   * it gets a dedicated message (not the generic symbol-table-full one). */
  CompileResult cr;
  const char *src = "let twelvecharss = 1\n";   /* name is 12 chars */
  globals_reset();
  compiler_compile_source(src, (uint16_t)strlen(src), &cr);
  EXPECT(cr.err != SE_OK, 1);
  EXPECT(cr.err_msg != (const char *)0, 2);
  EXPECT(strcmp(cr.err_msg, "name too long") == 0, 3);
  return 0;
}

int test_compile_func_name_too_long_is_error(void) {
  /* The dedicated message also covers function-name declarations. */
  CompileResult cr;
  const char *src = "func twelvecharss() {}\n";   /* name is 12 chars */
  globals_reset();
  compiler_compile_source(src, (uint16_t)strlen(src), &cr);
  EXPECT(cr.err != SE_OK, 1);
  EXPECT(cr.err_msg != (const char *)0, 2);
  EXPECT(strcmp(cr.err_msg, "name too long") == 0, 3);
  return 0;
}

int test_compile_ident_at_limit_ok(void) {
  /* An 11-char name (the cap) compiles and runs. */
  char buf[64];
  EXPECT(run_and_capture("let elevenchars = 7\nprint(elevenchars)\n",
                         buf, sizeof(buf)) == 0, 1);
  EXPECT(strcmp(buf, "7\n") == 0, 2);
  return 0;
}

int test_compile_semicolons(void) {
  char buf[64];
  EXPECT(run_and_capture("let x = 21; let y = 2; print(x * y)\n",
                         buf, sizeof(buf)) == 0, 1);
  EXPECT(strcmp(buf, "42\n") == 0, 2);
  return 0;
}

int test_compile_negative_literal(void) {
  char buf[64];
  EXPECT(run_and_capture("let n = -1000\nprint(n)\n",
                         buf, sizeof(buf)) == 0, 1);
  EXPECT(strcmp(buf, "-1000\n") == 0, 2);
  return 0;
}

int test_compile_string_literal(void) {
  char buf[64];
  EXPECT(run_and_capture("print(\"hello\")\n", buf, sizeof(buf)) == 0, 1);
  EXPECT(strcmp(buf, "hello\n") == 0, 2);
  return 0;
}

int test_compile_string_escapes(void) {
  char buf[64];
  /* \n, \t, \\ all decode at compile time. */
  EXPECT(run_and_capture("print(\"a\\nb\\tc\\\\d\")\n",
                         buf, sizeof(buf)) == 0, 1);
  EXPECT(strcmp(buf, "a\nb\tc\\d\n") == 0, 2);
  return 0;
}

int test_compile_string_concat(void) {
  char buf[64];
  EXPECT(run_and_capture("print(\"foo\" + \"bar\")\n",
                         buf, sizeof(buf)) == 0, 1);
  EXPECT(strcmp(buf, "foobar\n") == 0, 2);
  return 0;
}

int test_compile_string_concat_var(void) {
  char buf[64];
  EXPECT(run_and_capture("let g = \"hi\"\nprint(g + \", world\")\n",
                         buf, sizeof(buf)) == 0, 1);
  EXPECT(strcmp(buf, "hi, world\n") == 0, 2);
  return 0;
}

int test_compile_string_interp_int(void) {
  char buf[64];
  EXPECT(run_and_capture("let n = 42\nprint(\"answer: \\(n)\")\n",
                         buf, sizeof(buf)) == 0, 1);
  EXPECT(strcmp(buf, "answer: 42\n") == 0, 2);
  return 0;
}

int test_compile_string_interp_expr(void) {
  char buf[64];
  EXPECT(run_and_capture("let x = 3\nlet y = 4\n"
                         "print(\"x*y = \\(x * y)\")\n",
                         buf, sizeof(buf)) == 0, 1);
  EXPECT(strcmp(buf, "x*y = 12\n") == 0, 2);
  return 0;
}

int test_compile_string_interp_bool(void) {
  char buf[64];
  EXPECT(run_and_capture("print(\"ok=\\(true), \\(false)\")\n",
                         buf, sizeof(buf)) == 0, 1);
  EXPECT(strcmp(buf, "ok=true, false\n") == 0, 2);
  return 0;
}

int test_compile_if_then(void) {
  char buf[64];
  EXPECT(run_and_capture("if 1 < 2 { print(\"yes\") }\n",
                         buf, sizeof(buf)) == 0, 1);
  EXPECT(strcmp(buf, "yes\n") == 0, 2);
  return 0;
}

int test_compile_if_false_skips(void) {
  char buf[64];
  EXPECT(run_and_capture("if 1 > 2 { print(\"no\") }\nprint(\"done\")\n",
                         buf, sizeof(buf)) == 0, 1);
  EXPECT(strcmp(buf, "done\n") == 0, 2);
  return 0;
}

int test_compile_if_else(void) {
  char buf[64];
  EXPECT(run_and_capture("if 1 > 2 { print(\"a\") } else { print(\"b\") }\n",
                         buf, sizeof(buf)) == 0, 1);
  EXPECT(strcmp(buf, "b\n") == 0, 2);
  return 0;
}

int test_compile_if_else_if_chain(void) {
  char buf[64];
  EXPECT(run_and_capture(
      "let n = 0\n"
      "if n > 0 { print(\"pos\") }\n"
      "else if n < 0 { print(\"neg\") }\n"
      "else { print(\"zero\") }\n",
      buf, sizeof(buf)) == 0, 1);
  EXPECT(strcmp(buf, "zero\n") == 0, 2);
  return 0;
}

int test_compile_while(void) {
  char buf[64];
  EXPECT(run_and_capture(
      "var n = 0\nwhile n < 3 { n = n + 1; print(n) }\n",
      buf, sizeof(buf)) == 0, 1);
  EXPECT(strcmp(buf, "1\n2\n3\n") == 0, 2);
  return 0;
}

int test_compile_while_false_never_runs(void) {
  char buf[64];
  EXPECT(run_and_capture(
      "while false { print(\"x\") }\nprint(\"done\")\n",
      buf, sizeof(buf)) == 0, 1);
  EXPECT(strcmp(buf, "done\n") == 0, 2);
  return 0;
}

int test_compile_nested_if(void) {
  char buf[128];
  EXPECT(run_and_capture(
      "let x = 5\n"
      "if x > 0 { if x > 3 { print(\"big\") } else { print(\"small\") } }\n",
      buf, sizeof(buf)) == 0, 1);
  EXPECT(strcmp(buf, "big\n") == 0, 2);
  return 0;
}

int test_compile_nested_loops(void) {
  /* Regression for the cc65 -Cl static-local clobber (docs/contributing/LESSONS.md
   * 2026-06-03): on the buggy compiler an outer loop containing another
   * loop ran exactly once, and an `if`-no-else with a nested `if`
   * mis-jumped. The host (real stack locals) was never affected, so this
   * only guards the desugar logic + the placeholder-LIFO balance (ASan
   * flags an over/underflow of parser.h `if_end`); the cc65 path is
   * verified on the emulator / hardware. */
  char buf[64];
  EXPECT(run_and_capture(
      "var g = 0\n"
      "for i in 1...3 { for j in 1...3 { g = g + 1 } }\n"
      "print(g)\n"
      "func cl(_ n: Int) -> Int {\n"
      "  if n > 0 { if n > 10 { return 2 }\n return 1 }\n"
      "  return 0\n"
      "}\n"
      "print(cl(5))\n"
      "print(cl(50))\n",
      buf, sizeof(buf)) == 0, 1);
  EXPECT(strcmp(buf, "9\n1\n2\n") == 0, 2);
  return 0;
}

int test_compile_for_half_open_range(void) {
  char buf[64];
  EXPECT(run_and_capture("for i in 0..<3 { print(i) }\n",
                         buf, sizeof(buf)) == 0, 1);
  EXPECT(strcmp(buf, "0\n1\n2\n") == 0, 2);
  return 0;
}

int test_compile_for_closed_range(void) {
  char buf[64];
  EXPECT(run_and_capture("for i in 1...3 { print(i) }\n",
                         buf, sizeof(buf)) == 0, 1);
  EXPECT(strcmp(buf, "1\n2\n3\n") == 0, 2);
  return 0;
}

int test_compile_for_empty_range(void) {
  char buf[64];
  EXPECT(run_and_capture("for i in 5..<5 { print(i) }\nprint(\"done\")\n",
                         buf, sizeof(buf)) == 0, 1);
  EXPECT(strcmp(buf, "done\n") == 0, 2);
  return 0;
}

int test_compile_fizzbuzz(void) {
  /* The demo. Apple II would do 1...15; size-bound to 1...5 here
   * for the host buffer. */
  char buf[64];
  EXPECT(run_and_capture(
      "for i in 1...5 {\n"
      "  if i % 15 == 0 { print(\"FizzBuzz\") }\n"
      "  else if i % 3 == 0 { print(\"Fizz\") }\n"
      "  else if i % 5 == 0 { print(\"Buzz\") }\n"
      "  else { print(i) }\n"
      "}\n",
      buf, sizeof(buf)) == 0, 1);
  EXPECT(strcmp(buf, "1\n2\nFizz\n4\nBuzz\n") == 0, 2);
  return 0;
}

/* Function-call mechanics. */

int test_compile_func_void(void) {
  char buf[64];
  EXPECT(run_and_capture(
      "func g() { print(\"hi\") }\ng()\n",
      buf, sizeof(buf)) == 0, 1);
  EXPECT(strcmp(buf, "hi\n") == 0, 2);
  return 0;
}

int test_compile_func_one_arg_underscore(void) {
  char buf[64];
  EXPECT(run_and_capture(
      "func sq(_ x: Int) -> Int { return x * x }\n"
      "print(sq(7))\n",
      buf, sizeof(buf)) == 0, 1);
  EXPECT(strcmp(buf, "49\n") == 0, 2);
  return 0;
}

int test_compile_func_labeled_args(void) {
  char buf[64];
  EXPECT(run_and_capture(
      "func add(a: Int, b: Int) -> Int { return a + b }\n"
      "print(add(a: 3, b: 4))\n",
      buf, sizeof(buf)) == 0, 1);
  EXPECT(strcmp(buf, "7\n") == 0, 2);
  return 0;
}

int test_compile_func_local_let(void) {
  /* A local `let` inside a function body becomes a local slot above
   * the parameters; subsequent references read via OP_GET_LOCAL. */
  char buf[64];
  EXPECT(run_and_capture(
      "func twice(_ x: Int) -> Int {\n"
      "  let doubled = x + x\n"
      "  return doubled\n"
      "}\n"
      "print(twice(21))\n",
      buf, sizeof(buf)) == 0, 1);
  EXPECT(strcmp(buf, "42\n") == 0, 2);
  return 0;
}

int test_compile_func_recursion(void) {
  /* fact(4) needs exactly VM_CALL_FRAMES=4 frames at deepest. */
  char buf[64];
  EXPECT(run_and_capture(
      "func fact(_ n: Int) -> Int {\n"
      "  if n <= 1 { return 1 }\n"
      "  return n * fact(n - 1)\n"
      "}\n"
      "print(fact(4))\n",
      buf, sizeof(buf)) == 0, 1);
  EXPECT(strcmp(buf, "24\n") == 0, 2);
  return 0;
}

int test_compile_func_recursion_overflow(void) {
  /* fact(5) needs 5 frames; cap is 4. Should runtime-error. */
  CompileResult cr;
  swiftii_err_t rc;
  extern unsigned char *bcbuf_data(void);
  globals_reset();
  vm_reset_globals();
  compiler_compile_source(
      "func fact(_ n: Int) -> Int {\n"
      "  if n <= 1 { return 1 }\n"
      "  return n * fact(n - 1)\n"
      "}\n"
      "print(fact(5))\n", 96, &cr);
  EXPECT(cr.err == SE_OK, 1);
  platform_init();
  rc = vm_run(bcbuf_data(), cr.program_start, cr.bc_len);
  platform_shutdown();
  EXPECT(rc != SE_OK, 2);
  return 0;
}

int test_compile_func_nested_call(void) {
  char buf[64];
  EXPECT(run_and_capture(
      "func sq(_ x: Int) -> Int { return x * x }\n"
      "func add(a: Int, b: Int) -> Int { return a + b }\n"
      "print(sq(add(a: 1, b: 2)))\n",
      buf, sizeof(buf)) == 0, 1);
  EXPECT(strcmp(buf, "9\n") == 0, 2);
  return 0;
}

int test_compile_func_return_value_required(void) {
  /* A function declared `-> Int` whose body falls through without a
   * return is a compile error (no flow analysis). */
  CompileResult cr;
  globals_reset();
  compiler_compile_source(
      "func f() -> Int { let x = 1 }\n", 31, &cr);
  EXPECT(cr.err != SE_OK, 1);
  return 0;
}

int test_compile_func_wrong_arity(void) {
  /* Calling a function with the wrong number of args is a compile
   * error (signature is set before body compile). */
  CompileResult cr;
  globals_reset();
  compiler_compile_source(
      "func add(a: Int, b: Int) -> Int { return a + b }\n"
      "print(add(a: 1))\n", 67, &cr);
  EXPECT(cr.err != SE_OK, 1);
  return 0;
}

int test_compile_return_outside_function(void) {
  /* `return` at top level is a compile error. */
  CompileResult cr;
  globals_reset();
  compiler_compile_source("return 5\n", 9, &cr);
  EXPECT(cr.err != SE_OK, 1);
  return 0;
}

int test_compile_func_local_shadows_global(void) {
  /* A local named the same as a global is preferred inside the
   * function body (lexical scope), even when the global also exists. */
  char buf[64];
  EXPECT(run_and_capture(
      "var x = 100\n"
      "func f(_ x: Int) -> Int { return x + 1 }\n"
      "print(f(5))\n"
      "print(x)\n",
      buf, sizeof(buf)) == 0, 1);
  EXPECT(strcmp(buf, "6\n100\n") == 0, 2);
  return 0;
}

/* Break, print(_:terminator:), readLine. */

int test_compile_break_in_while(void) {
  char buf[64];
  EXPECT(run_and_capture(
      "var i = 0\n"
      "while true { i = i + 1; if i > 3 { break }; print(i) }\n"
      "print(\"done\")\n",
      buf, sizeof(buf)) == 0, 1);
  EXPECT(strcmp(buf, "1\n2\n3\ndone\n") == 0, 2);
  return 0;
}

int test_compile_break_in_for_in(void) {
  char buf[64];
  EXPECT(run_and_capture(
      "for n in 1...10 { if n > 3 { break }; print(n) }\n",
      buf, sizeof(buf)) == 0, 1);
  EXPECT(strcmp(buf, "1\n2\n3\n") == 0, 2);
  return 0;
}

int test_compile_break_outside_loop_is_error(void) {
  CompileResult cr;
  globals_reset();
  compiler_compile_source("break\n", 6, &cr);
  EXPECT(cr.err != SE_OK, 1);
  return 0;
}

int test_compile_multiple_breaks_in_one_loop(void) {
  /* Two separate break sites in the same loop body — loops.c caps
   * at 4 per loop, so 2 should be no problem. */
  char buf[64];
  EXPECT(run_and_capture(
      "for n in 1...10 {\n"
      "  if n == 4 { break }\n"
      "  if n == 7 { break }\n"
      "  print(n)\n"
      "}\n",
      buf, sizeof(buf)) == 0, 1);
  EXPECT(strcmp(buf, "1\n2\n3\n") == 0, 2);
  return 0;
}

int test_compile_print_terminator_empty(void) {
  char buf[64];
  EXPECT(run_and_capture(
      "print(\"x: \", terminator: \"\"); print(42)\n",
      buf, sizeof(buf)) == 0, 1);
  EXPECT(strcmp(buf, "x: 42\n") == 0, 2);
  return 0;
}

int test_compile_print_terminator_space(void) {
  char buf[64];
  EXPECT(run_and_capture(
      "print(1, terminator: \" \")\n"
      "print(2, terminator: \" \")\n"
      "print(3)\n",
      buf, sizeof(buf)) == 0, 1);
  EXPECT(strcmp(buf, "1 2 3\n") == 0, 2);
  return 0;
}

int test_compile_print_terminator_only(void) {
  /* `print(terminator: "x")` with no value arg is just the
   * terminator. */
  char buf[64];
  EXPECT(run_and_capture(
      "print(terminator: \"hi\\n\")\n",
      buf, sizeof(buf)) == 0, 1);
  EXPECT(strcmp(buf, "hi\n") == 0, 2);
  return 0;
}

int test_compile_print_terminator_expr(void) {
  char buf[64];
  EXPECT(run_and_capture(
      "let sep = \" | \"\n"
      "print(1, terminator: sep)\n"
      "print(2)\n",
      buf, sizeof(buf)) == 0, 1);
  EXPECT(strcmp(buf, "1 | 2\n") == 0, 2);
  return 0;
}

int test_compile_readline_eof(void) {
  /* readLine + nil-comparison + branch compiles cleanly. The actual
   * EOF / line-read behavior is exercised by the integration test
   * 007_readline.swift which can pipe a stdin fixture. */
  const char *src =
      "let line = readLine()\nif line == nil { print(\"eof\") }\n";
  CompileResult cr;
  globals_reset();
  compiler_compile_source(src, (uint16_t)strlen(src), &cr);
  EXPECT(cr.err == SE_OK, 1);
  return 0;
}

/* Optionals. */

int test_compile_force_unwrap_some(void) {
  /* `5!` is a no-op on a non-nil value; the program prints 5. */
  char buf[64];
  EXPECT(run_and_capture(
      "let a: Int? = 5\nprint(a!)\n",
      buf, sizeof(buf)) == 0, 1);
  EXPECT(strcmp(buf, "5\n") == 0, 2);
  return 0;
}

int test_compile_nil_coalesce_takes_default(void) {
  char buf[64];
  EXPECT(run_and_capture(
      "let b: Int? = nil\nprint(b ?? 99)\n",
      buf, sizeof(buf)) == 0, 1);
  EXPECT(strcmp(buf, "99\n") == 0, 2);
  return 0;
}

int test_compile_nil_coalesce_takes_value(void) {
  char buf[64];
  EXPECT(run_and_capture(
      "let a: Int? = 5\nprint(a ?? 99)\n",
      buf, sizeof(buf)) == 0, 1);
  EXPECT(strcmp(buf, "5\n") == 0, 2);
  return 0;
}

int test_compile_if_let_some(void) {
  char buf[64];
  EXPECT(run_and_capture(
      "let a: Int? = 5\n"
      "if let v = a { print(v + 1) }\n"
      "print(\"done\")\n",
      buf, sizeof(buf)) == 0, 1);
  EXPECT(strcmp(buf, "6\ndone\n") == 0, 2);
  return 0;
}

int test_compile_if_let_nil_skips(void) {
  char buf[64];
  EXPECT(run_and_capture(
      "let b: Int? = nil\n"
      "if let v = b { print(\"never\") }\n"
      "print(\"done\")\n",
      buf, sizeof(buf)) == 0, 1);
  EXPECT(strcmp(buf, "done\n") == 0, 2);
  return 0;
}

int test_compile_if_let_else(void) {
  /* Top-level if-let with an else arm — some path and nil
   * path each take the right branch. */
  char buf[64];
  EXPECT(run_and_capture(
      "let a: Int? = 5\n"
      "if let v = a { print(\"some \\(v)\") } else { print(\"none\") }\n"
      "let b: Int? = nil\n"
      "if let v = b { print(\"some \\(v)\") } else { print(\"none\") }\n",
      buf, sizeof(buf)) == 0, 1);
  EXPECT(strcmp(buf, "some 5\nnone\n") == 0, 2);
  return 0;
}

int test_compile_if_let_inside_function(void) {
  /* If-let inside a function body, with an else arm and a
   * nested local in the bound branch — scope-pop must keep the frame
   * balanced so post-block code reads the right slots. */
  char buf[96];
  EXPECT(run_and_capture(
      "func describe(_ x: Int?) -> String {\n"
      "  if let n = x { let d = n * 2; return \"got \\(d)\" }\n"
      "  else { return \"none\" }\n"
      "}\n"
      "func sumOrZero(_ x: Int?, _ base: Int) -> Int {\n"
      "  var total = base\n"
      "  if let n = x { let bonus = n + 1; total = total + bonus }\n"
      "  return total\n"
      "}\n"
      "print(describe(7))\n"
      "print(describe(nil))\n"
      "print(sumOrZero(10, 100))\n"
      "print(sumOrZero(nil, 100))\n",
      buf, sizeof(buf)) == 0, 1);
  EXPECT(strcmp(buf, "got 14\nnone\n111\n100\n") == 0, 2);
  return 0;
}

/* Arrays (minimum viable subset). */

int test_compile_array_literal_and_count(void) {
  char buf[64];
  EXPECT(run_and_capture(
      "let xs = [10, 20, 30]\nprint(xs.count)\nprint(xs[1])\n",
      buf, sizeof(buf)) == 0, 1);
  EXPECT(strcmp(buf, "3\n20\n") == 0, 2);
  return 0;
}

int test_compile_array_empty_literal(void) {
  char buf[64];
  EXPECT(run_and_capture(
      "let xs = []\nprint(xs.count)\n",
      buf, sizeof(buf)) == 0, 1);
  EXPECT(strcmp(buf, "0\n") == 0, 2);
  return 0;
}

int test_compile_array_append_with_capture(void) {
  /* Requires explicit write-back: `xs = xs.append(v)`.
   * The shorter `xs.append(v)` form compiles but does NOT update the
   * variable; that polish lands. */
  char buf[64];
  EXPECT(run_and_capture(
      "var xs = [1]\n"
      "xs = xs.append(2)\n"
      "xs = xs.append(3)\n"
      "xs = xs.append(4)\n"
      "xs = xs.append(5)\n"
      "print(xs.count)\nprint(xs[4])\n",
      buf, sizeof(buf)) == 0, 1);
  EXPECT(strcmp(buf, "5\n5\n") == 0, 2);
  return 0;
}

int test_compile_array_methods(void) {
  /* RemoveLast (returns element, mutates in place),
   * removeAll (empties in place), contains (Bool, OP_EQ equality). */
  char buf[96];
  EXPECT(run_and_capture(
      "var ws = [10, 20, 30]\n"
      "let last = ws.removeLast()\n"
      "print(last)\n"             /* 30 */
      "print(ws.count)\n"         /* 2 */
      "print(ws.contains(20))\n"  /* true */
      "print(ws.contains(30))\n"  /* false (removed) */
      "ws.removeAll()\n"
      "print(ws.isEmpty)\n"       /* true */
      "print(ws.count)\n",        /* 0 */
      buf, sizeof(buf)) == 0, 1);
  EXPECT(strcmp(buf, "30\n2\ntrue\nfalse\ntrue\n0\n") == 0, 2);
  return 0;
}

int test_compile_array_remove_last_empty_runtime_error(void) {
  CompileResult cr;
  swiftii_err_t rc;
  extern unsigned char *bcbuf_data(void);
  globals_reset();
  vm_reset_globals();
  {
    const char *prog =
        "var xs = [1]\nlet a = xs.removeLast()\nlet b = xs.removeLast()\n";
    compiler_compile_source(prog, (uint16_t)strlen(prog), &cr);
  }
  EXPECT(cr.err == SE_OK, 1);
  platform_init();
  rc = vm_run(bcbuf_data(), cr.program_start, cr.bc_len);
  platform_shutdown();
  EXPECT(rc != SE_OK, 2);   /* second removeLast on empty array */
  return 0;
}

int test_compile_array_subscript_oob_runtime_error(void) {
  CompileResult cr;
  swiftii_err_t rc;
  extern unsigned char *bcbuf_data(void);
  globals_reset();
  vm_reset_globals();
  compiler_compile_source(
      "let xs = [1, 2]\nprint(xs[5])\n", 29, &cr);
  EXPECT(cr.err == SE_OK, 1);
  platform_init();
  rc = vm_run(bcbuf_data(), cr.program_start, cr.bc_len);
  platform_shutdown();
  EXPECT(rc != SE_OK, 2);
  return 0;
}

int test_compile_func_arg_type_check_ok(void) {
  char buf[64];
  EXPECT(run_and_capture(
      "func f(_ x: Int) -> Int { return x + 1 }\n"
      "print(f(41))\n",
      buf, sizeof(buf)) == 0, 1);
  EXPECT(strcmp(buf, "42\n") == 0, 2);
  return 0;
}

int test_compile_func_arg_type_mismatch(void) {
  CompileResult cr;
  globals_reset();
  compiler_compile_source(
      "func f(_ x: Int) -> Int { return x + 1 }\nprint(f(\"hi\"))\n",
      56, &cr);
  EXPECT(cr.err != SE_OK, 1);
  return 0;
}

int test_compile_min_max(void) {
  char buf[64];
  EXPECT(run_and_capture(
      "print(min(3, 7))\n"
      "print(max(3, 7))\n"
      "print(min(-2, 5))\n"
      "print(max(-2, 5))\n",
      buf, sizeof(buf)) == 0, 1);
  EXPECT(strcmp(buf, "3\n7\n-2\n5\n") == 0, 2);
  return 0;
}

int test_compile_min_wrong_type(void) {
  CompileResult cr;
  globals_reset();
  compiler_compile_source("print(min(1, \"x\"))\n", 19, &cr);
  EXPECT(cr.err != SE_OK, 1);
  return 0;
}

/* String(_ n: Int) -> String. Compiles to OP_STR_INTERP_I
 * (reusing the existing polymorphic Int→heap-string path), so this
 * exercises the type-tracker hook plus the concatenation flow that
 * makes the converted value usable. */
int test_compile_string_of_int(void) {
  char buf[64];
  EXPECT(run_and_capture(
      "print(String(42))\n"
      "print(String(-7))\n"
      "print(\"n=\" + String(0))\n",
      buf, sizeof(buf)) == 0, 1);
  EXPECT(strcmp(buf, "42\n-7\nn=0\n") == 0, 2);
  return 0;
}

int test_compile_string_of_int_wrong_type(void) {
  CompileResult cr;
  globals_reset();
  compiler_compile_source("print(String(\"hi\"))\n", 20, &cr);
  EXPECT(cr.err != SE_OK, 1);
  return 0;
}

/* `Asc(_ s: String) -> Int` — first byte
 * of a string as an Int. The worker (xlc_asc) lives in XLC on
 * SWIFTSAT and in normal CODE on host; this test reaches it via the
 * BUILTIN_ASC dispatch through end-to-end run_and_capture, the same
 * path the REPL/file_runner uses. */
int test_compile_asc(void) {
  char buf[64];
  EXPECT(run_and_capture(
      "print(asc(\"a\"))\n"
      "print(asc(\"Z\"))\n"
      "print(asc(\"hello\"))\n"
      "let s = \"swift\"\n"
      "print(asc(s))\n",
      buf, sizeof(buf)) == 0, 1);
  EXPECT(strcmp(buf, "97\n90\n104\n115\n") == 0, 2);
  return 0;
}

int test_compile_asc_wrong_type(void) {
  CompileResult cr;
  globals_reset();
  compiler_compile_source("print(asc(42))\n", 15, &cr);
  EXPECT(cr.err != SE_OK, 1);
  return 0;
}

/* `Chr(_ n: Int) -> String` — inverse of
 * asc. Second XLC-resident built-in; runs through the same generic
 * dispatch path as asc via end-to-end run_and_capture. */
int test_compile_chr(void) {
  char buf[64];
  EXPECT(run_and_capture(
      "print(chr(65))\n"
      "print(chr(122))\n"
      "print(chr(asc(\"Swift\")))\n"
      "print(chr(72) + chr(105))\n",
      buf, sizeof(buf)) == 0, 1);
  EXPECT(strcmp(buf, "A\nz\nS\nHi\n") == 0, 2);
  return 0;
}

int test_compile_chr_wrong_type(void) {
  CompileResult cr;
  globals_reset();
  compiler_compile_source("print(chr(\"x\"))\n", 16, &cr);
  EXPECT(cr.err != SE_OK, 1);
  return 0;
}

/* `Int(_ s: String) -> Int?` — failable string→int.
 * XLC builtin like asc/chr; reaches the dispatcher via the generic
 * XLC path through run_and_capture. Valid parses unwrap; invalid /
 * overflow ones yield nil (exercised via `??`). */
int test_compile_int_from_string(void) {
  char buf[96];
  EXPECT(run_and_capture(
      "print(Int(\"42\")!)\n"
      "print(Int(\"-7\")!)\n"
      "print(Int(\"+5\")!)\n"
      "print(Int(\"32767\")!)\n"
      "print(Int(\"-32768\")!)\n"
      "print(Int(\"x\") ?? -1)\n"
      "print(Int(\"\") ?? -1)\n"
      "print(Int(\"9x\") ?? -1)\n"
      "print(Int(\"32768\") ?? -1)\n",
      buf, sizeof(buf)) == 0, 1);
  EXPECT(strcmp(buf, "42\n-7\n5\n32767\n-32768\n-1\n-1\n-1\n-1\n") == 0, 2);
  return 0;
}

int test_compile_int_from_string_wrong_type(void) {
  CompileResult cr;
  globals_reset();
  compiler_compile_source("print(Int(7)!)\n", 15, &cr);
  EXPECT(cr.err != SE_OK, 1);
  return 0;
}

/* Platform builtins (SWIFTSAT/host). peek returns 0 on host
 * (no raw memory access), poke is a no-op, home routes to the host
 * ANSI clear-screen escape. */
int test_compile_peek_poke(void) {
  char buf[64];
  EXPECT(run_and_capture(
      "poke(49200, 0)\n"
      "let v = peek(1024)\n"
      "print(v)\n"
      "print(peek(49200))\n",
      buf, sizeof(buf)) == 0, 1);
  EXPECT(strcmp(buf, "0\n0\n") == 0, 2);
  return 0;
}

int test_compile_home(void) {
  char buf[64];
  /* home() emits the host clear-screen escape, then the print runs. */
  EXPECT(run_and_capture("home()\nprint(\"ok\")\n", buf, sizeof(buf)) == 0, 1);
  EXPECT(strcmp(buf, "\x1b[2J\x1b[Hok\n") == 0, 2);
  return 0;
}

int test_compile_peek_wrong_type(void) {
  CompileResult cr;
  globals_reset();
  compiler_compile_source("print(peek(\"x\"))\n", 17, &cr);
  EXPECT(cr.err != SE_OK, 1);
  return 0;
}

int test_compile_poke_wrong_type(void) {
  CompileResult cr;
  globals_reset();
  compiler_compile_source("poke(1, \"x\")\n", 13, &cr);
  EXPECT(cr.err != SE_OK, 1);
  return 0;
}

/* Cursor positioning. htab/vtab are no-ops on the
 * host, so they compile + run cleanly and leave the print output
 * unchanged. */
int test_compile_htab_vtab(void) {
  char buf[64];
  EXPECT(run_and_capture(
      "vtab(3)\n"
      "htab(5)\n"
      "print(\"ok\")\n",
      buf, sizeof(buf)) == 0, 1);
  EXPECT(strcmp(buf, "ok\n") == 0, 2);
  return 0;
}

int test_compile_htab_wrong_type(void) {
  CompileResult cr;
  globals_reset();
  compiler_compile_source("htab(\"x\")\n", 10, &cr);
  EXPECT(cr.err != SE_OK, 1);
  return 0;
}

int test_compile_vtab_range_runtime(void) {
  CompileResult cr;
  swiftii_err_t rc;
  extern unsigned char *bcbuf_data(void);
  globals_reset();
  vm_reset_globals();
  /* vtab(0) is out of the 1..24 range -> SE_RUNTIME. */
  compiler_compile_source("vtab(0)\n", 8, &cr);
  EXPECT(cr.err == SE_OK, 1);
  platform_init();
  rc = vm_run(bcbuf_data(), cr.program_start, cr.bc_len);
  platform_shutdown();
  EXPECT(rc != SE_OK, 2);
  return 0;
}

/* Low-res graphics. gr/text/color/plot are no-ops on
 * the host, so a GR program compiles + runs cleanly and only its
 * print() reaches the captured output. */
int test_compile_gr(void) {
  char buf[64];
  EXPECT(run_and_capture(
      "gr()\n"
      "color(5)\n"
      "plot(0, 0)\n"
      "plot(39, 39)\n"
      "text()\n"
      "print(\"drawn\")\n",
      buf, sizeof(buf)) == 0, 1);
  EXPECT(strcmp(buf, "drawn\n") == 0, 2);
  return 0;
}

/* Text80()/text() are no-ops on the host (the //e
 * 80-col firmware path is target-only), so they compile + run cleanly
 * and leave print output unchanged. Both take no args. */
int test_compile_text80(void) {
  char buf[64];
  EXPECT(run_and_capture(
      "text80()\n"
      "print(\"a\")\n"
      "text()\n"
      "print(\"b\")\n",
      buf, sizeof(buf)) == 0, 1);
  EXPECT(strcmp(buf, "a\nb\n") == 0, 2);
  return 0;
}

int test_compile_text80_wrong_arity(void) {
  CompileResult cr;
  globals_reset();
  compiler_compile_source("text80(5)\n", 10, &cr);
  EXPECT(cr.err != SE_OK, 1);
  return 0;
}

int test_compile_color_wrong_type(void) {
  CompileResult cr;
  globals_reset();
  compiler_compile_source("color(\"x\")\n", 11, &cr);
  EXPECT(cr.err != SE_OK, 1);
  return 0;
}

int test_compile_color_range_runtime(void) {
  CompileResult cr;
  swiftii_err_t rc;
  extern unsigned char *bcbuf_data(void);
  globals_reset();
  vm_reset_globals();
  /* color(16) is out of the 0..15 range -> SE_RUNTIME. */
  compiler_compile_source("color(16)\n", 10, &cr);
  EXPECT(cr.err == SE_OK, 1);
  platform_init();
  rc = vm_run(bcbuf_data(), cr.program_start, cr.bc_len);
  platform_shutdown();
  EXPECT(rc != SE_OK, 2);
  return 0;
}

int test_compile_plot_range_runtime(void) {
  CompileResult cr;
  swiftii_err_t rc;
  extern unsigned char *bcbuf_data(void);
  globals_reset();
  vm_reset_globals();
  /* plot(40, 0): x out of the 0..39 range -> SE_RUNTIME. */
  compiler_compile_source("plot(40, 0)\n", 12, &cr);
  EXPECT(cr.err == SE_OK, 1);
  platform_init();
  rc = vm_run(bcbuf_data(), cr.program_start, cr.bc_len);
  platform_shutdown();
  EXPECT(rc != SE_OK, 2);
  return 0;
}

/* grFull(): full-screen 40x48. plot accepts y up to 47 in this mode
 * (vs 39 in mixed gr()), so a full-screen GR program with y=47 runs
 * cleanly on the host (no-op draw). */
int test_compile_gr_full(void) {
  char buf[64];
  EXPECT(run_and_capture(
      "grFull()\n"
      "color(9)\n"
      "plot(0, 47)\n"
      "plot(39, 0)\n"
      "text()\n"
      "print(\"full\")\n",
      buf, sizeof(buf)) == 0, 1);
  EXPECT(strcmp(buf, "full\n") == 0, 2);
  return 0;
}

int test_compile_plot_full_oob_runtime(void) {
  CompileResult cr;
  swiftii_err_t rc;
  extern unsigned char *bcbuf_data(void);
  globals_reset();
  vm_reset_globals();
  /* Even in full-screen mode y=48 is out of the 0..47 range. */
  compiler_compile_source("grFull()\nplot(0, 48)\n", 21, &cr);
  EXPECT(cr.err == SE_OK, 1);
  platform_init();
  rc = vm_run(bcbuf_data(), cr.program_start, cr.bc_len);
  platform_shutdown();
  EXPECT(rc != SE_OK, 2);
  return 0;
}

/* Mixed gr() caps y at 39 even though full-screen allows 47: switching
 * back to gr() after grFull() must restore the tighter bound. */
int test_compile_plot_mixed_y_runtime(void) {
  CompileResult cr;
  swiftii_err_t rc;
  extern unsigned char *bcbuf_data(void);
  globals_reset();
  vm_reset_globals();
  compiler_compile_source("grFull()\ngr()\nplot(0, 40)\n", 26, &cr);
  EXPECT(cr.err == SE_OK, 1);
  platform_init();
  rc = vm_run(bcbuf_data(), cr.program_start, cr.bc_len);
  platform_shutdown();
  EXPECT(rc != SE_OK, 2);
  return 0;
}

/* Hlin/vlin (positional: endpoints then line coord) +
 * scrn. On the host the drawing is a no-op and scrn returns 0; only the
 * prints land. */
int test_compile_hlin_vlin_scrn(void) {
  char buf[64];
  EXPECT(run_and_capture(
      "gr()\n"
      "color(5)\n"
      "hlin(0, 39, 0)\n"
      "vlin(0, 39, 0)\n"
      "print(scrn(0, 0))\n",
      buf, sizeof(buf)) == 0, 1);
  EXPECT(strcmp(buf, "0\n") == 0, 2);
  return 0;
}

int test_compile_hlin_wrong_arity(void) {
  CompileResult cr;
  globals_reset();
  /* hlin takes exactly 3 args (x1, x2, y). */
  compiler_compile_source("hlin(0, 39)\n", 12, &cr);
  EXPECT(cr.err != SE_OK, 1);
  return 0;
}

int test_compile_hlin_range_runtime(void) {
  CompileResult cr;
  swiftii_err_t rc;
  extern unsigned char *bcbuf_data(void);
  globals_reset();
  vm_reset_globals();
  /* x2 = 40 is out of the 0..39 range. */
  compiler_compile_source("hlin(0, 40, 0)\n", 15, &cr);
  EXPECT(cr.err == SE_OK, 1);
  platform_init();
  rc = vm_run(bcbuf_data(), cr.program_start, cr.bc_len);
  platform_shutdown();
  EXPECT(rc != SE_OK, 2);
  return 0;
}

int test_compile_array_is_empty(void) {
  char buf[64];
  EXPECT(run_and_capture(
      "let a = [1, 2]\n"
      "let b: [Int] = []\n"
      "print(a.isEmpty)\n"
      "print(b.isEmpty)\n",
      buf, sizeof(buf)) == 0, 1);
  EXPECT(strcmp(buf, "false\ntrue\n") == 0, 2);
  return 0;
}

int test_compile_array_subscript_set(void) {
  /* `Xs[i] = v` overwrites a slot in place. */
  char buf[64];
  EXPECT(run_and_capture(
      "var xs = [1, 2, 3]\n"
      "xs[1] = 99\n"
      "print(xs[0])\n"
      "print(xs[1])\n"
      "print(xs[2])\n",
      buf, sizeof(buf)) == 0, 1);
  EXPECT(strcmp(buf, "1\n99\n3\n") == 0, 2);
  return 0;
}

int test_compile_array_subscript_set_let_is_const(void) {
  CompileResult cr;
  globals_reset();
  compiler_compile_source(
      "let xs = [1, 2, 3]\nxs[0] = 99\n", 30, &cr);
  EXPECT(cr.err != SE_OK, 1);
  return 0;
}

int test_compile_array_subscript_set_type_mismatch(void) {
  CompileResult cr;
  globals_reset();
  compiler_compile_source(
      "var xs: [Int] = [1, 2]\nxs[0] = \"hi\"\n", 36, &cr);
  EXPECT(cr.err != SE_OK, 1);
  return 0;
}

int test_compile_array_subscript_set_oob_runtime_error(void) {
  CompileResult cr;
  swiftii_err_t rc;
  extern unsigned char *bcbuf_data(void);
  globals_reset();
  vm_reset_globals();
  compiler_compile_source(
      "var xs = [1, 2]\nxs[5] = 99\n", 27, &cr);
  EXPECT(cr.err == SE_OK, 1);
  platform_init();
  rc = vm_run(bcbuf_data(), cr.program_start, cr.bc_len);
  platform_shutdown();
  EXPECT(rc != SE_OK, 2);
  return 0;
}

int test_compile_array_in_function(void) {
  /* Pass an array through a function parameter and read elements.
   * Annotation is `[Int]` per the c4a `[T]` form. Earlier
   * Versions of this test wrote `_ a: Int` because the
   * `[T]` form wasn't parseable — pre-tracker the type was
   * accepted without checking. The c5 validator now requires the
   * declaration to match. */
  char buf[64];
  EXPECT(run_and_capture(
      "func sumThree(_ a: [Int]) -> Int {\n"
      "  return a[0] + a[1] + a[2]\n"
      "}\n"
      "let xs = [10, 20, 30]\n"
      "print(sumThree(xs))\n",
      buf, sizeof(buf)) == 0, 1);
  EXPECT(strcmp(buf, "60\n") == 0, 2);
  return 0;
}
