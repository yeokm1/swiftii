/* REPL read-compile-eval loop.
 *
 * Per-iteration: read a line via the platform keyboard, check for a
 * meta-command (`:help` and friends), otherwise compile the line in
 * REPL mode and run it. Globals and the compiler's symbol table
 * persist across iterations so `let x = 1` on one line and `print(x)`
 * on the next produces 1.
 *
 * REPL-only auto-print: bare top-level expressions implicitly emit
 * `print(value)` so `1 + 2` yields `3` (BASIC / Python-style). The
 * compiler keys this off `compiler_compile_source_repl` (see
 * `emit_expr_stmt_end` in src/compiler/statements.c); file mode
 * discards bare-expression values via OP_POP. `let`/`var` declarations
 * and assignments are silent.
 *
 * The Swift-LLDB-style polish layer that originally shipped with this
 * REPL (numbered prompts, `name: Type = value` echoes, `$R<n>`
 * auto-results, `OP_REPL_ECHO`) was removed 2026-05-23 to fund
 * deferred features — see ROADMAP "Maybe / probably never"
 * item 19.
 *
 * Outcomes:
 *   - `:mem` and the heap-clearing extension of `:reset` — shipped.
 * - Function redefinition with notice — shipped on the //e
 *     REPL binaries (config.h SWIFTII_FUNC_REDEF = WITH_IIE && !WITH_SWB);
 *     see funcs.c / parse_func_decl. The II+ binaries are at the ProDOS
 *     ceiling so `:reset` stays the workaround there.
 *   - Multi-line input with brace/paren tracking — dropped from
 *     the v1 REPL; the in-launcher editor is the home for block-level
 *     authoring.
 */
#ifdef __CC65__
/* REPL driver: one iteration per user input line. Latency is bounded
 * by the human keystroke, so relocating to LC has no perceptible
 * cost. The hot path (VM dispatch + heap + value-system) stays in
 * main-RAM CODE. */
#pragma code-name (push, "LC")
#endif

#include "repl.h"

#include <stdint.h>

#include "../common/config.h"
#include "../common/errors.h"
#include "../compiler/bcbuf.h"
#include "../compiler/compiler.h"
#include "../compiler/globals.h"
#include "../lexer/lexer.h"
#include "../runtime/heap.h"
#include "../vm/vm.h"
#include "../platform/platform.h"
#include "metacmds.h"

#define LINE_BUF_SIZE 256

static char s_line[LINE_BUF_SIZE];

static void show_banner(void) {
  /* Build date + copyright are shown by the boot launcher (menu + About), so
   * the REPL banner keeps just the machine-tagged name + version — which
   * also reclaims LC the if/else-if reentrancy fix needs on the tight
   * SWIFTIIP / SWIFTSAT binaries. */
#if defined(WITH_SWIFTSAT)
  /* Saturn 128K extras (ships on the II+ disk; II+/earlier + Saturn). */
  platform_write_str("SwiftII ][+ Saturn " SWIFTII_VERSION "\n");
#if defined(WITH_80COL)
  /* SWIFTSAT does NOT auto-enter 80-col; if a Videx Videoterm is detected,
   * tell the user how to opt in. Silent on a non-Videx II+. */
  if (platform_videx_present()) {
    platform_write_str("Videx 80 column detected: text80()\n");
  }
#endif
#elif defined(WITH_SWIFTAUX)
  /* //e aux extras (ships on the //e disk; WITH_IIE -> native case). */
  platform_write_str("SwiftII //e aux " SWIFTII_VERSION "\n");
#elif defined(WITH_EXTRAS)
  platform_write_str("SwiftIIX " SWIFTII_VERSION "\n");
#elif defined(WITH_IIE)
  /* //e-disk lite (SWIFTIIE.SYSTEM): native case + lowercase. */
  platform_write_str("SwiftII //e " SWIFTII_VERSION "\n");
#else
  /* II+-disk lite (SWIFTIIP.SYSTEM): //+ typing model, caps. */
  platform_write_str("SwiftII ][+ " SWIFTII_VERSION "\n");
#endif
  /* Keep the "Type :" prefix: tests/repl/runner.sh strips the banner with
   * a sed range ending at /^Type :/. */
  platform_write_str("Type :help :list :quit\n");
}

static void show_prompt(void) {
  platform_write_str("> ");
}

/* Compile a source buffer in REPL mode and run it, reporting compile and
 * runtime errors to the console. Shared by the interactive loop and the
 * Startup path so there is a single copy of the
 * compile/run/error logic. */
static void run_source(const char *src, uint16_t len) {
  CompileResult cr;

  compiler_compile_source_repl(src, len, &cr);
  if (cr.err != SE_OK) {
    platform_write_str("compile error: ");
    if (cr.err_msg) platform_write_str(cr.err_msg);
    platform_write_str("\n");
    return;
  }
  if (vm_run(bcbuf_data(), cr.program_start, cr.bc_len) != SE_OK) {
    platform_write_str("runtime error\n");
  }
}

int repl_run(void) {
  int16_t len;
  metacmd_result_t mres;

  globals_reset();
  vm_reset_globals();
  heap_reset();

#ifdef __CC65__
  /* If the boot launcher staged a program (it did the disk read —
   * no MLI/:load in the interpreter), run it once and skip the banner:
   * the program's output stands on its own, then we drop to the prompt.
   * With nothing staged, show the banner as usual. We compile straight
   * from the staged region, so no source buffer is needed here; a trailing
   * bare expression auto-prints (REPL-mode compile), harmless for a file.
   * The bound keeps a stale/garbage length (e.g. a direct, non-launcher boot)
   * from lexing past the staged page into the $C0xx soft switches;
   * 1..FILE_SRC_SIZE collapses to one unsigned compare. No one-shot clear
   * is needed — repl_run reads this once and the launcher re-deposits a length
   * (0 = nothing) on every chain. */
  {
    uint16_t staged = *(volatile uint16_t *)STAGED_LEN_ADDR;
    if ((uint16_t)(staged - 1) < FILE_SRC_SIZE) {
      run_source((const char *)STAGED_SRC_ADDR, staged);
    } else {
      show_banner();
    }
  }
#else
  show_banner();
#endif

  for (;;) {
    show_prompt();

    len = repl_read_line(s_line, (uint16_t)sizeof(s_line));
    if (len <= 0) {
      platform_write_str("\n");
      break;
    }

    mres = metacmds_handle(s_line, (uint16_t)len);
    if (mres == METACMD_QUIT) break;
    if (mres == METACMD_HANDLED) continue;

    /* The line we receive is already canonical lowercase ASCII: on the
     * Apple II target keyboard.c runs the input layer over the typed
     * bytes before returning; on the host stdin already delivers
     * canonical bytes (rev 3 of design doc 003). */
    run_source(s_line, (uint16_t)len);
  }

  return 0;
}

#ifdef __CC65__
#pragma code-name (pop)
#endif
