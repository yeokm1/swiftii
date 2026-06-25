/* Family B Runner entry point (doc 015).
 *
 * A MAIN-only binary (empty LC, MLI intact) that reads a compiled `.swb`
 * image from disk and executes it. It sheds the compiler entirely (lexer +
 * parser + emit), so the whole MAIN window is the VM + runtime + all
 * builtins INLINE — no Saturn/aux bank — which is why a file program's
 * graphics/sound run on any machine. The bytecode runs IN PLACE inside the
 * image buffer (swb_open_image), so no second bytecode buffer is needed and
 * the runtime heap gets the lion's share of the ~21 KB window.
 *
 * Per-machine (-DWITH_IIE) for the keyboard/display typing model only.
 */

#include <stdint.h>

#include "../common/config.h"
#include "../common/errors.h"
#include "../platform/platform.h"
#include "../runtime/prodos.h"
#if defined(WITH_TESTLOG)
#include "../runtime/file_io.h"   /* userfile_append for the TESTLOG verdict */
#endif
#include "../vm/vm.h"
#include "../vm/bcwin.h"
#include "../swb/swb.h"
#if defined(BC_STORE_SATURN) && defined(__CC65__)
#include "../common/aux_store.h"   /* saturn_bc_init */
#endif

#ifndef WITH_AUX_BC
/* The whole .swb image (header + bytecode + const heap + funcs). Sized to
 * the LARGEST .swb the flat Tier 1 Compiler can emit (header 12 + bc 1,834
 * + consts 768 + 24*4 funcs = 2,710 B, + slack) — the buffers must track each
 * other (see Makefile RUNNER_DEFS for the budget history). The bytecode
 * is executed in place from here. BSS. */
#ifndef SWB_IMAGE_SIZE
#define SWB_IMAGE_SIZE 3328
#endif
static unsigned char s_image[SWB_IMAGE_SIZE];
#else
/* Paged Runner (//e aux): the bytecode is streamed into aux RAM (bcwin),
 * never held whole in MAIN, so the only MAIN buffers are a small staging
 * chunk and the const-heap+funcs tail. SWB_TAIL_CAP bounds the largest
 * (const-pool + funcs) section a .swb may carry — the Compiler's const pool
 * is capped at its HEAP_SIZE (896) so 1 KB covers it + the 24*4 funcs. */
#ifndef SWB_TAIL_CAP
#define SWB_TAIL_CAP 1024
#endif
#define BC_STAGE_CHUNK 512
static unsigned char s_chunk[BC_STAGE_CHUNK];
static unsigned char s_tail[SWB_TAIL_CAP];
#endif

#ifdef __CC65__
/* Chain a SYS file (the launcher) over $2000 — chain.s. */
extern void __fastcall__ chain_exec(unsigned char refnum);
#endif

static void puts_(const char *s) {
  while (*s) platform_putc(*s++);
}

#ifdef __CC65__
static void puthex_(unsigned char b) {
  static const char H[] = "0123456789ABCDEF";
  platform_putc(H[b >> 4]);
  platform_putc(H[b & 0x0F]);
}
#endif

#ifdef __CC65__
/* The .swb to run: a length-prefixed path the Compiler (its derived output)
 * or the launcher (a .swb picked in the file selector) left at EDIT_PATH_ADDR.
 * Falls back to the fixed name if none was staged. */
static char s_path[72];
static const char *swb_path(void) {
  const unsigned char *p = (const unsigned char *)EDIT_PATH_ADDR;
  uint8_t plen = p[0];
  uint8_t i;
  if (plen == 0 || plen >= (uint8_t)sizeof s_path) return SWB_PATH;
  for (i = 0; i < plen; ++i) s_path[i] = (char)p[1 + i];
  s_path[plen] = '\0';
  return s_path;
}
#endif

#ifndef WITH_AUX_BC
/* Read SWB_PATH into s_image; returns length or -1 open / -2 too big. */
static long load_image(const char *path) {
  pf_handle h;
  uint16_t len = 0;
  int n;
  h = pf_open_read(path);
  if (h == PF_BAD) return -1;
  for (;;) {
    if (len >= (uint16_t)SWB_IMAGE_SIZE) { pf_close(h); return -2; }
    n = pf_read(h, s_image + len, (uint16_t)(SWB_IMAGE_SIZE - len));
    if (n <= 0) break;
    len = (uint16_t)(len + n);
  }
  pf_close(h);
  return (long)len;
}
#else
/* Read up to `want` bytes, looping over ProDOS short reads; returns the
 * count actually read (< want only at EOF). */
static uint16_t read_exact(pf_handle h, unsigned char *buf, uint16_t want) {
  uint16_t got = 0;
  int n;
  while (got < want) {
    n = pf_read(h, buf + got, (uint16_t)(want - got));
    if (n <= 0) break;
    got = (uint16_t)(got + n);
  }
  return got;
}

/* Paged load: stream the .swb bytecode into aux (bcwin) and restore the
 * const-heap + funcs tail in MAIN. Returns 0 ok, 1 open fail, 2 too big,
 * 3 bad/truncated image. */
static int load_paged(const char *path, uint16_t *program_start,
                      uint16_t *bc_len) {
  pf_handle h;
  unsigned char hdr[SWB_HEADER_SIZE];
  uint16_t heap_len, tail_len, off, want;
  uint8_t funcs_n;
  swb_err_t se;

  h = pf_open_read(path);
  if (h == PF_BAD) return 1;

  if (read_exact(h, hdr, SWB_HEADER_SIZE) != SWB_HEADER_SIZE) {
    pf_close(h); return 3;
  }
  se = swb_header_info(hdr, program_start, bc_len, &heap_len, &funcs_n);
  if (se != SWB_OK) { pf_close(h); return 3; }

  tail_len = (uint16_t)(heap_len + (uint16_t)funcs_n * SWB_FUNC_SIZE);
  if (tail_len > (uint16_t)SWB_TAIL_CAP) { pf_close(h); return 2; }

  /* Bytecode section -> aux park, a chunk at a time. */
  off = 0;
  while (off < *bc_len) {
    want = (uint16_t)(*bc_len - off);
    if (want > BC_STAGE_CHUNK) want = BC_STAGE_CHUNK;
    if (read_exact(h, s_chunk, want) != want) { pf_close(h); return 3; }
    bcwin_stage(off, s_chunk, want);
    off = (uint16_t)(off + want);
  }

  /* const-heap + funcs tail -> MAIN, then rebuild the singletons. */
  if (read_exact(h, s_tail, tail_len) != tail_len) { pf_close(h); return 3; }
  pf_close(h);

  se = swb_load_tail(s_tail, heap_len, funcs_n, *bc_len);
  return (se == SWB_OK) ? 0 : 3;
}
#endif

static int run_file(const char *path) {
  uint16_t prog_start, bc_len;
  swiftii_err_t rc;

  /* Visible progress for the chain gap: a multi-KB .swb takes a moment to read
   * before the program's own output starts. Path on its own line (colon label)
   * so a long path doesn't wrap past 40 columns. */
  puts_("Running:\r");
  puts_(path);
  puts_("\r");

#ifdef WITH_AUX_BC
  {
    int lr = load_paged(path, &prog_start, &bc_len);
    if (lr != 0) {
      if (lr == 2) {
        puts_("program too big for memory\r");
      } else if (lr == 3) {
        puts_("bad .swb image\r");
      } else {
        puts_("cannot open ");
        puts_(path);
#ifdef __CC65__
        puts_(" err=$");
        puthex_(pf_errno);
#endif
        puts_("\r");
      }
      return 1;
    }
  }
  vm_reset_globals();
  rc = vm_run((const unsigned char *)0, prog_start, bc_len);
#else
  {
    const unsigned char *bc;
    swb_err_t se;
    long n = load_image(path);
    if (n < 0) {
      if (n == -2) {
        puts_("program too big for memory\r");
      } else {
        puts_("cannot open ");
        puts_(path);
#ifdef __CC65__
        puts_(" err=$");
        puthex_(pf_errno);
#endif
        puts_("\r");
      }
      return 1;
    }
    se = swb_open_image(s_image, (uint16_t)n, &bc, &prog_start, &bc_len);
    if (se != SWB_OK) { puts_("bad .swb image\r"); return 1; }
    vm_reset_globals();
    rc = vm_run(bc, prog_start, bc_len);
  }
#endif

  if (rc == SE_BREAK) { puts_("\r*break*\r"); return 1; }
  if (rc != SE_OK) { puts_("\rruntime error\r"); return 1; }
  return 0;
}

int main(
#ifndef __CC65__
    int argc, char **argv
#else
    void
#endif
) {
  const char *path;
  int rc;

  if (platform_init() != 0) return 1;

#if defined(BC_STORE_SATURN) && defined(__CC65__)
  /* Tier 2: patch the Saturn store driver for the card the launcher detected
   * (slot parked at SX_SAT_SLOT_ADDR) before the first paged store access. */
  saturn_bc_init(*(const unsigned char *)SX_SAT_SLOT_ADDR);
#endif

#ifdef __CC65__
  path = swb_path();
#else
  path = (argc >= 2) ? argv[1] : SWB_PATH;
#endif

#if defined(WITH_TESTLOG)
  /* On-target test sweep (design doc 018): if TESTRUN.SYSTEM set the $1B08
   * marker before this run, arm the "FAIL"-token watcher over the program's
   * print output so we can record a PASS/FAIL verdict below. */
  testlog_begin((unsigned char)(
      *(volatile unsigned char *)TESTRUN_MODE_ADDR == TESTRUN_MODE_MAGIC0 &&
      *(volatile unsigned char *)(TESTRUN_MODE_ADDR + 1) == TESTRUN_MODE_MAGIC1));
#endif

  rc = run_file(path);

#ifdef __CC65__
  /* The program is done (or errored) but the screen still shows its output.
   * Drop any type-ahead (e.g. the Ctrl-C that broke the run) so the pause
   * needs a fresh keypress, then wait, then chain back to the launcher (NO
   * cold reboot) — it reopens the editor / file selector via its LASTRUN
   * note, per the run's origin. */
  *(volatile unsigned char *)0xC010 = 0;   /* clear stale keyboard strobe */
  /* In an on-target test sweep, TESTRUN.SYSTEM set the $1B08 marker (design
   * doc 018) before this run — auto-advance after a short interval so the
   * FBTESTS sweep runs hands-off (no keypress). The marker is consumed
   * (cleared) on read, so a later normal [X] run keeps the original indefinite
   * "Press any key" pause. The marker rides the $1B00 handoff page through the
   * COMPILER->RUNNER chain, like the Saturn slot at $1B04. */
  if (*(volatile unsigned char *)TESTRUN_MODE_ADDR == TESTRUN_MODE_MAGIC0 &&
      *(volatile unsigned char *)(TESTRUN_MODE_ADDR + 1) == TESTRUN_MODE_MAGIC1) {
#if defined(WITH_TESTLOG)
    /* Record this test's verdict (P/F, in run order) on the data disk's
     * TESTLOG so TESTRUN.SYSTEM's results page can list the failures. */
    userfile_append("/SWIFTII.DATA/TESTLOG",
                    (const unsigned char *)(testlog_failed() ? "F" : "P"), 1);
#endif
    *(volatile unsigned char *)TESTRUN_MODE_ADDR = 0;   /* consume the marker */
    puts_("\r\rNext test in ");
    {
      unsigned char s;
      for (s = 4; s >= 1; s--) {            /* visible 4-3-2-1 countdown */
        platform_putc((char)('0' + s));
        platform_putc(' ');
        platform_wait_ms(1000);
      }
    }
  } else {
    puts_("\r\rPress any key to continue...");
    while ((*(volatile unsigned char *)0xC000 & 0x80) == 0) { /* wait */ }
  }
  *(volatile unsigned char *)0xC010 = 0;   /* clear keyboard strobe */

  /* A program may have left graphics on — get back to 40-col text page 1 so
   * the launcher draws into a text screen. STORES, not reads: cc65 elides a
   * discarded volatile read entirely (LESSONS 2026-05-30), and $C00C is
   * write-activated on the //e anyway. */
  *(volatile unsigned char *)0xC051 = 0;   /* TEXT  */
  *(volatile unsigned char *)0xC054 = 0;   /* PAGE1 */
  *(volatile unsigned char *)0xC00C = 0;   /* 80COL off */
#if defined(WITH_80COL) && !defined(WITH_IIE)
  /* II+ Videx: a program may have entered 80-col (text80()) and left it on so
   * its output stays visible through the "Press any key" pause (which prints
   * on the Videoterm). Before chaining to the 40-col launcher, revert: AN0 off
   * hands the display back to the Apple screen and CSW->COUT1 unhooks the
   * firmware, so the launcher draws on the 40-col page, not the 80-col card.
   * platform_text40() (the text() worker, already linked) does exactly this;
   * reusing it is smaller than open-coding the stores. The //e Runner uses the
   * $C00C reset above and is excluded so it stays byte-identical. */
  platform_text40();
#endif
  {
    pf_handle h = pf_open_read(LAUNCHER_PATH);
    if (h != PF_BAD) chain_exec((unsigned char)h);   /* never returns */
  }
#endif

  platform_shutdown();                 /* host / fallback */
  return rc;
}
