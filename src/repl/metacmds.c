/* REPL meta-command dispatch.
 *
 * Recognised at the start of any REPL input line whose first non-space
 * character is ':'. `:mem` and the heap-clearing extension of `:reset`
 * landed. `:list` prints the bindings as `let/var name = value`
 * (the type-annotated `: Type` form was dropped 2026-06-06 — its two ctype
 * renderers cost ~550 B, reclaimed to fund compiler fixes on the II+
 * binaries; the name + current value is the useful part and reuses the
 * print helpers already in the binary).
 *
 * Relocated to the language card (LC segment):
 * meta-commands fire once per user keystroke at REPL time, so the
 * call-into-LC overhead is irrelevant compared to their runtime
 * importance, and pulling ~1 KB out of main RAM frees BSS headroom
 * that the new function-call machinery needs. cc65 generates a copy
 * routine in STARTUP that relocates LC code from MAIN to $D000-$DFFF
 * at boot. The cfg's __LCSIZE__ = $0C00 ceiling is shared with the
 * other LC-relocated files (file_runner.c) — see docs/contributing/MEMORY_MAP.md. */
#ifdef __CC65__
#pragma code-name (push, "LC")
#endif

#include "metacmds.h"

#include <stdint.h>

#include "../common/config.h"
#include "../common/types.h"
#include "../compiler/globals.h"
#include "../platform/platform.h"
#include "../runtime/builtins.h"
#include "../runtime/heap.h"
#include "../vm/vm.h"

static void trim(const char **pline, uint16_t *plen) {
  const char *s = *pline;
  uint16_t n = *plen;
  while (n > 0 && (*s == ' ' || *s == '\t')) { ++s; --n; }
  while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' ||
                   s[n - 1] == ' '  || s[n - 1] == '\t')) {
    --n;
  }
  *pline = s;
  *plen = n;
}

/* ASCII-case-insensitive match against the (lowercase) reference `kw`,
 * so `:HELP` works on a //+ keyboard. */
static int eq(const char *s, uint16_t len, const char *kw) {
  uint16_t i;
  unsigned char tc;
  for (i = 0; i < len; ++i) {
    if (kw[i] == 0) return 0;
    tc = (unsigned char)s[i];
    if (tc >= 'A' && tc <= 'Z') tc = (unsigned char)(tc + 32);
    if ((unsigned char)kw[i] != tc) return 0;
  }
  return kw[len] == 0;
}

static void cmd_help(void) {
  /* Single-line command summary (<=40 cols so it doesn't wrap). `^d=exit`
   * advertises the Ctrl-D EOF exit (keyboard.c). The //+ digraph /
   * case-marker guide that used to print here moved to docs/using/LANGUAGE.md
   * (2026-05-24) — kept the documentation, dropped the
   * ~50 B RODATA string in favour of headroom for the platform builtins. */
  platform_write_str(":help :list :mem :reset :quit  ^d=exit\n");
#if defined(WITH_SWIFTSAT) && defined(WITH_80COL)
  /* SWIFTSAT (II+ Saturn) exposes the Videx width switch as ordinary builtins
   * and does NOT auto-enter 80-col — advertise them so a Videoterm user knows
   * how to toggle. Gated to SWIFTSAT so the //e binaries stay byte-identical
   * (they also have text80()/text(), but the user asked for this on SWIFTSAT). */
  platform_write_str("text80() text() = 80/40 columns\n");
#endif
}

/* `:list` — print each global binding in definition order as
 * `let name = value` / `var name = value`. The original (removed
 * 2026-06-06) also rendered the declared type (`: Int`, `: [Int]`, ...),
 * but that pulled in builtins_write_ctype + builtins_type_name (~550 B);
 * the name + current value carries most of the value and reuses
 * builtins_print_value (already linked for `:mem` / `print`). A binding
 * declared without a value yet (`var x: Int`) prints just its name. */
static void cmd_list(void) {
  uint8_t n;
  uint8_t i;
  uint8_t j;
  const char *name;
  Value v;

  n = globals_count();
  if (n == 0) {
    platform_write_str("(none)\n");
    return;
  }
  for (i = 0; i < n; ++i) {
    name = globals_get_name(i);
    platform_write_str(globals_is_let(i) ? "let " : "var ");
    if (name) {
      for (j = 0; j < IDENT_MAX; ++j) {
        if (name[j] == 0) break;
        platform_putc(name[j]);
      }
    }
    if (vm_get_global(i, &v)) {
      platform_write_str(" = ");
      builtins_print_value(&v);
    }
    platform_putc('\n');
  }
}

static void cmd_reset(void) {
  /* Order matters: vm_reset_globals releases the global Values' heap
   * refs (so refcounts drop to zero); globals_reset wipes the compiler
   * symbol table; heap_reset wipes the heap entirely so any leaked
   * mid-stack allocations are reclaimed too. */
  vm_reset_globals();
  globals_reset();
  heap_reset();
  platform_write_str("cleared\n");
}

/* `:home` removed prep 2026-05-24. The `home()`
 * builtin (BUILTIN_HOME / opcode $08) was attempted as a replacement
 * but pulled when cc65 produced ~193 B vs ~50 B source estimate;
 * the per-builtin cost in OP_CALL_BUILTIN's else-if chain is the
 * bottleneck. Users on the REPL can still clear via the underlying
 * platform call indirectly (none today — accepted regression for v1).
 * Re-add `home()` when OP_CALL_BUILTIN gets restructured. */

static void cmd_mem(void) {
  /* Print free heap as an Int via builtins_print_value — reuses the
   * digit-formatting loop that builtins already owns. HEAP_SIZE is
   * 2048 (config.h) so the count always fits in int16_t. */
  Value v;
  uint16_t n;
  n = heap_free_bytes();
  v.tag = T_INT;
  v.lo = (unsigned char)(n & 0xFF);
  v.hi = (unsigned char)(n >> 8);
  platform_write_str("heap: ");
  builtins_print_value(&v);
  builtins_print_newline();
}

/* The `:xlc` smoke-test meta-cmd was
 * removed in commit 3b once the first real XLC builtin (`asc`)
 * shipped end-to-end. End-user verification of the XLC mechanism on
 * SWIFTSAT now goes through `print(asc("a"))` instead — same code
 * path, no extra REPL surface to maintain. */

metacmd_result_t metacmds_handle(const char *line, uint16_t len) {
  trim(&line, &len);
  if (len == 0 || line[0] != ':') return METACMD_NOT_META;

  if (eq(line, len, ":help"))  { cmd_help();  return METACMD_HANDLED; }
  if (eq(line, len, ":list"))  { cmd_list();  return METACMD_HANDLED; }
  if (eq(line, len, ":mem"))   { cmd_mem();   return METACMD_HANDLED; }
  if (eq(line, len, ":reset")) { cmd_reset(); return METACMD_HANDLED; }
  if (eq(line, len, ":quit"))  { return METACMD_QUIT; }

  platform_write_str("?\n");
  return METACMD_HANDLED;
}

#ifdef __CC65__
#pragma code-name (pop)
#endif
