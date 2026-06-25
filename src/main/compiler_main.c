/* Family B Compiler entry point (doc 015).
 *
 * A MAIN-only binary (empty LC, ProDOS MLI intact — see
 * swiftii-compiler.cfg) that reads SOURCE.swift from disk into a big RAM
 * window, compiles it in one pass, and streams the result to SWB_PATH as a
 * `.swb` image. Because it has no language card it can do file I/O and chain
 * the Runner — the combined interpreters can't, which is the whole reason
 * Family B is split into separate tools. The `.swb` on disk IS the handoff to
 * the Runner; only the selected path rides in low RAM across the chain.
 *
 * Not machine-specific (no keyboard/display typing model) — one shared
 * Compiler binary serves both the II+ and //e Family B disks.
 */

#include <stdint.h>

#include "../common/config.h"
#include "../common/errors.h"
#include "../platform/platform.h"
#include "../compiler/compiler.h"
#include "../compiler/bcbuf.h"
#include "../compiler/globals.h"
#include "../compiler/parser.h"   /* refill_progress reads p->L for percent */
#include "../compiler/srcwin.h"
#include "../runtime/heap.h"
#include "../runtime/prodos.h"
#include "../swb/swb.h"
#if defined(BC_STORE_SATURN) && defined(__CC65__)
#include "../common/aux_store.h"   /* saturn_bc_init */
#endif

/* Source window, sized via -DFILE_SRC_SIZE on the compiler cl65 line.
 * Tier 2 streams the file through it (see s_win below); the window only
 * has to hold one statement at a time, not the whole source.
 *
 * (Doc 016): on target the window is NOT in BSS — it lives
 * in the free launcher-handoff low RAM at $0C00-$1BFF (4 KB), which the
 * chain READ (writes $2000+) leaves untouched and nothing in the MAIN-only
 * Compiler uses once the staged path is copied out. That hands the old BSS
 * window back to the bytecode arena + symbol tables. Two order-of-use
 * invariants:
 *   1. source_path() copies the launcher's staged path out of
 *      EDIT_PATH_ADDR ($0C00 — the SAME address) into s_path[] BEFORE
 *      load_source() first writes the window (main() calls them in that
 *      order; the editor follows the same copy-out-then-reuse protocol).
 *   2. stage_swb_for_runner() reuses $0C00 only after compilation is done.
 * The window abuts the MLI I/O buffer at $1C00 — it must not grow past it. */
#ifdef __CC65__
#if (0x0C00 + FILE_SRC_SIZE) > 0x1C00
#error "FILE_SRC_SIZE overruns the $0C00-$1BFF source window (MLI buffer at $1C00)"
#endif
#define s_src ((char *)0x0C00u)
#else
static char s_src[FILE_SRC_SIZE];
#endif

static void puts_(const char *s) {
  while (*s) platform_putc(*s++);
}

/* Print a uint16 in decimal (for the compile-error line number). 16-bit
 * div/mod only — no 32-bit cc65 runtime pulled in (cf. refill_progress). */
static void putdec_(uint16_t v) {
  char buf[5];
  unsigned char i = 0;
  if (v == 0) { platform_putc('0'); return; }
  while (v) { buf[i++] = (char)('0' + (v % 10)); v = (uint16_t)(v / 10); }
  while (i) platform_putc(buf[--i]);
}

/* swb_write_stream sink: write to the open .swb handle. */
static int swb_sink(void *ctx, const unsigned char *buf, uint16_t n) {
  pf_handle h = *(pf_handle *)ctx;
  return pf_write(h, buf, n);
}

/* Append the MLI error behind a failed .swb create/write, naming the
 * common case: $48 volume full — a 140 KB Family B disk holds only a few
 * KB of slack for .swb output, so "disk full" is the error a user can
 * actually act on (delete old .SWB files / compile to the drive-2 data
 * disk). Host builds have no MLI errno. */
static void write_err_detail(void) {
#ifdef __CC65__
  static const char H[] = "0123456789ABCDEF";
  puts_(" err=$");
  platform_putc(H[pf_errno >> 4]);
  platform_putc(H[pf_errno & 0x0F]);
  if (pf_errno == 0x48) puts_(" disk full");
#endif
  puts_("\r");
}

/* (Doc 016): the window is a sliding view of the source
 * file, refilled at statement boundaries (srcwin_refill via the parser
 * hook) — source size is disk-bounded, not window-bounded. The file stays
 * open across the whole compile; the .swb is opened only after
 * srcwin_close() (one MLI I/O buffer, one open at a time). */
static SrcWin s_win;

/* Right-justified "NN%" rewritten in place at the start of the current
 * line (platform_htab is 1-based), so the progress line ticks 22% ->
 * 45% -> ... -> 100% without scrolling. */
static void print_pct(uint8_t pct) {
  platform_htab(1);
  if (pct >= 100) {
    puts_("100%");
    return;
  }
  platform_putc(pct >= 10 ? (char)('0' + pct / 10) : ' ');
  platform_putc((char)('0' + pct % 10));
  platform_putc('%');
}

/* Progress hook: srcwin_refill, then on each completed window slide show
 * percent-of-file compiled. Bytes consumed = bytes read minus what is
 * still ahead of the lexer in the window. 16-bit math on purpose —
 * consumed/(total/100) is within 1% and avoids dragging cc65's ~700 B
 * of 32-bit mul/div runtime into a binary with no room for it. */
static void refill_progress(struct parser *p) {
  uint16_t before = s_win.slides;
  uint16_t denom = (uint16_t)(s_win.total / 100u);
  srcwin_refill(p);
  if (s_win.slides != before && denom != 0) {
    uint16_t consumed = (uint16_t)(s_win.rd - (p->L.len - p->L.pos));
    uint16_t pct = consumed / denom;
    if (pct > 99) pct = 99;       /* 100% prints only on completion */
    print_pct((uint8_t)pct);
  }
}

/* The source path: the launcher stages a length-prefixed path at
 * EDIT_PATH_ADDR before chaining us; fall back to DEFAULT_SRC_PATH. */
#ifdef __CC65__
static char s_path[64];
static const char *source_path(void) {
  const unsigned char *p = (const unsigned char *)EDIT_PATH_ADDR;
  uint8_t plen = p[0];
  uint8_t i;
  if (plen == 0 || plen >= sizeof s_path) return DEFAULT_SRC_PATH;
  for (i = 0; i < plen; ++i) s_path[i] = (char)p[1 + i];
  s_path[plen] = '\0';
  return s_path;
}
#endif

/* Derive the .swb output path from the source path: same folder + base name,
 * with the extension replaced by ".SWB" (e.g. /VOL/SAMPLES/GREET.SWIFT ->
 * /VOL/SAMPLES/GREET.SWB). The .swb sits next to its source so the file
 * selector can list + run it. */
static char s_swb_path[72];
static const char *derive_swb_path(const char *src) {
  uint16_t len = 0, slash = 0, dot = 0xFFFFu, i, base;
  while (src[len] && len < (uint16_t)(sizeof s_swb_path - 5)) {
    if (src[len] == '/') slash = len;
    ++len;
  }
  for (i = slash; i < len; ++i) if (src[i] == '.') dot = i;
  base = (dot != 0xFFFFu && dot > slash) ? dot : len;  /* keep dirs' dots */
  for (i = 0; i < base; ++i) s_swb_path[i] = src[i];
  s_swb_path[base + 0] = '.';
  s_swb_path[base + 1] = 'S';
  s_swb_path[base + 2] = 'W';
  s_swb_path[base + 3] = 'B';
  s_swb_path[base + 4] = '\0';
  return s_swb_path;
}

static int compile_file(const char *src_path, const char *swb_path) {
  CompileResult cr;
  pf_handle out;
  swb_err_t se;
  long n;
  unsigned char show_pct;

  /* Path on its own line so a long absolute path (e.g. a test on the data
   * disk) doesn't wrap past 40 columns. */
  puts_("Compiling:\r");
  puts_(src_path);
  puts_("\r");

#ifdef __CC65__
  /* Remove any stale .swb up front, as its OWN completed step, BEFORE the
   * compile. pf_open_write() also DESTROYs before CREATE, but a back-to-back
   * DESTROY+CREATE+write in one call spuriously returned ProDOS $48 (disk
   * full) when an old .swb was present, while deleting it separately first did
   * not (real-HW report). Doing it early (the compile then sits between the
   * delete and the create) mirrors the working "delete it yourself, then
   * compile" path. (void): a first compile has no .swb -> "not found", fine. */
  (void)pf_delete(swb_path);
#endif

  n = srcwin_open(&s_win, s_src, FILE_SRC_SIZE, src_path);
  if (n < 0) {
    puts_("cannot open source\r");
    return 1;
  }

  globals_reset();
  heap_reset();
  /* The percent line below this one updates in place (print_pct), from
   * " 0%" before the first statement to "100%" on completion. Files
   * smaller than the window (eof already set at open) never slide and
   * show no percent — they compile near-instantly anyway. */
  show_pct = !s_win.eof;
  if (show_pct) print_pct(0);
  compiler_set_refill(refill_progress, &s_win);
  compiler_compile_source(s_src, (uint16_t)n, &cr);
  srcwin_close(&s_win);
  if (cr.err == SE_OK && s_win.eof && show_pct) {
    print_pct(100);
  }
  puts_("\r");
  if (cr.err != SE_OK) {
    const char *m = cr.err_msg;
    if (!m) m = "?";
    puts_("compile error: line ");
    putdec_(cr.err_line);          /* locate the failing source line */
    puts_(": ");
    puts_(m);
    puts_("\r");
    if (!s_win.eof) {
      /* The lexer drained the window with file bytes still unread — a
       * single statement (or string literal) didn't fit the window. */
      puts_("(one statement is too long for memory)\r");
    }
    return 1;
  }
  if (!s_win.eof) {
    /* The parse "succeeded" without consuming the whole file: the lexer
     * EOF'd mid-statement at the window edge and the partial source still
     * parsed cleanly. Silently truncating the program would be worse than
     * any compile error — reject it. */
    puts_("compile error: one statement is too long for memory\r");
    return 1;
  }

  /* .swb as BIN ($06, aux $2000) so the launcher doesn't list it as .swift. */
  out = pf_open_write(swb_path, PF_TYPE_BIN, 0x2000);
  if (out == PF_BAD) {
    puts_("cannot create ");
    puts_(swb_path);
    write_err_detail();
    return 1;
  }
  se = swb_write_stream(bcbuf_data(), cr.bc_len, cr.program_start,
                        swb_sink, &out);
  pf_close(out);
  if (se != SWB_OK) {
    puts_("write error");
    write_err_detail();
    return 1;
  }

  puts_("Wrote:\r");
  puts_(swb_path);
  puts_("\r");
  return 0;
}

#ifdef __CC65__
/* Chain a SYS file (the Runner, or the launcher) over $2000 (chain.s). */
extern void __fastcall__ chain_exec(unsigned char refnum);

/* Wait for a keypress so a compile error is readable. Raw keyboard strobe —
 * the Compiler links no keyboard/conio. Clear the strobe first so a key
 * typed during the compile doesn't skip the pause. */
static void wait_key(void) {
  *(volatile unsigned char *)0xC010 = 0;   /* drop type-ahead */
  puts_("\rPress any key to continue...");
  while ((*(volatile unsigned char *)0xC000 & 0x80) == 0) { /* wait */ }
  *(volatile unsigned char *)0xC010 = 0;   /* clear strobe */
}

/* Return to the launcher WITHOUT a cold reboot: chain SWIFTII.SYSTEM, which
 * reopens the editor / file selector via its LASTRUN note. Falls back to a
 * cold reboot if it can't be opened. */
static void return_to_launcher(void) {
  pf_handle h = pf_open_read(LAUNCHER_PATH);
  if (h != PF_BAD) chain_exec((unsigned char)h);   /* never returns */
  platform_shutdown();                              /* fallback */
}

/* Hand the just-written .swb path to the Runner via EDIT_PATH_ADDR (the same
 * low-RAM page the launcher staged the source path in; it survives the chain
 * READ to $2000). The Runner reads it instead of a fixed name. */
static void stage_swb_for_runner(const char *p) {
  volatile unsigned char *d = (volatile unsigned char *)EDIT_PATH_ADDR;
  unsigned char n = 0;
  while (p[n] && n < 64) { d[1 + n] = (unsigned char)p[n]; ++n; }
  d[0] = n;
}
#endif

int main(
#ifndef __CC65__
    int argc, char **argv
#else
    void
#endif
) {
  const char *path;
  const char *swb;
  int rc;
#if defined(BC_STORE_SATURN) && defined(__CC65__)
  unsigned char sat_slot;
#endif

  if (platform_init() != 0) return 1;

#if defined(BC_STORE_SATURN) && defined(__CC65__)
  /* Tier 2: patch the Saturn store driver for the detected card (slot parked
   * at SX_SAT_SLOT_ADDR) before the first function-flush to the store. Stash the
   * slot: compiling clobbers the $1B00 handoff page (the source-read scratch
   * overlaps it), so we re-deposit the slot at $1B04 right before chaining the
   * Runner — otherwise the Runner reads a stray source byte as the slot and
   * patches saturn_bc.s to the wrong $C0xx switches (silent bytecode corruption
   * for any program big enough to actually page bytecode in). */
  sat_slot = *(const unsigned char *)SX_SAT_SLOT_ADDR;
  saturn_bc_init(sat_slot);
#endif

#ifdef __CC65__
  path = source_path();
#else
  path = (argc >= 2) ? argv[1] : DEFAULT_SRC_PATH;
#endif

  swb = derive_swb_path(path);         /* .swb next to the source */
  rc = compile_file(path, swb);

#ifdef __CC65__
  if (rc == 0) {
    /* Hand off to the Runner: tell it the .swb path, then chain RUNNER.SYSTEM
     * (installed alongside us) over $2000. */
    pf_handle r = pf_open_read(SWB_RUNNER_PATH);
    if (r != PF_BAD) {
      stage_swb_for_runner(swb);
#if defined(BC_STORE_SATURN) && defined(__CC65__)
      /* Re-deposit the Saturn slot the compile clobbered, so the chained Runner
       * reads the right value (the chain READ writes $2000+, leaving $1B04). */
      *(unsigned char *)SX_SAT_SLOT_ADDR = sat_slot;
#endif
      /* No "running..." here — the Runner prints "Running:" + path next. */
      chain_exec((unsigned char)r);   /* never returns */
    }
    puts_("cannot launch " SWB_RUNNER_PATH "\r");
  }
  /* Compile error (or the Runner was missing): let the message show, then go
   * back to the editor / file selector without a reboot. */
  wait_key();
  return_to_launcher();                /* never returns (chains the launcher) */
#endif

  platform_shutdown();                 /* host / fallback */
  return rc;
}
