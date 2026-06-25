/* Platform interface: every backend (host, apple2) implements these.
 *
 * Code outside src/platform/ should never include OS-specific headers
 * (stdio.h, ProDOS MLI headers, etc.) — go through this interface.
 *
 * Only needs init/print/exit. File I/O, keyboard, time, etc.
 * arrive later.
 */
#ifndef SWIFTII_PLATFORM_H
#define SWIFTII_PLATFORM_H

#include <stdint.h>

/* Called once at startup. Sets text mode, clears the screen on Apple II.
 * Returns 0 on success, nonzero on failure. */
int platform_init(void);

/* Write a length-prefixed buffer to the console. Treats $0D and $0A as
 * line terminators on Apple II (where COUT expects $0D). */
void platform_write(const char *buf, uint16_t len);

/* Convenience: write a NUL-terminated string. */
void platform_write_str(const char *s);

/* Write one character. */
void platform_putc(char c);

/* Clear the console and home the cursor (top-left). On the Apple II
 * this routes through cc65's `clrscr()`; on the host it emits an
 * ANSI clear-screen + cursor-home escape sequence. */
void platform_clear_screen(void);

/* Move the text cursor. `col`/`row` are 1-based (htab 1..40, vtab
 * 1..24) to match Applesoft HTAB/VTAB; the caller validates the range.
 * Each preserves the other axis. On the Apple II these drive cc65
 * conio's `gotoxy` (which tracks the same cursor the platform_write*
 * path uses); on the host they are no-ops so captured test output is
 * unaffected. */
void platform_htab(uint8_t col);
void platform_vtab(uint8_t row);

#if defined(WITH_GR_TEXTWIN)
/* Top row of the text scroll region (0 = full screen). gr()'s mixed mode sets
 * it to 20 to confine print() scrolling to the 4-line text window;
 * text()/grFull() reset it to 0. Defined in screen.c; written directly by the
 * GR builtins (no setter, to keep it out of SWIFTSAT's MAIN — see screen.c). */
extern unsigned char g_gr_scroll_top;
#endif

/* Read one line of input into `buf` (up to max_len - 1 chars plus the
 * trailing NUL). The returned line keeps the terminating '\n' when the
 * user hit Enter. Returns the number of bytes written (excluding the
 * NUL), or 0 on EOF / shutdown signal. The host backend reads from
 * stdin; the Apple II backend reads the keyboard with local echo and
 * basic backspace editing. */
int16_t platform_read_line(char *buf, uint16_t max_len);

/* platform_read_line: Runner-only Ctrl-C break sentinel (WITH_SWB). Distinct
 * from 0 (EOF) so the readLine builtin can raise SE_BREAK vs. push none. */
#define PLATFORM_READ_BREAK ((int16_t)-2)

/* The REPL prompt's line read. Identical to platform_read_line except on
 * SWIFTSAT, where it is a trampoline (keyboard.c) that runs platform_read_line
 * in Saturn bank 1 so its blinking cursor (which lives in the bank-1 XLC
 * overlay) is reachable. Everywhere else it is platform_read_line itself. */
#if defined(WITH_SWIFTSAT)
int16_t repl_read_line(char *buf, uint16_t max_len);
#else
#define repl_read_line platform_read_line
#endif

/* A 16-bit entropy value for seeding random(in:). The Apple II has no clock,
 * so (as Applesoft's RND did via the monitor's KEYIN counter) the only real
 * entropy is human keypress timing: the Apple II backend returns a counter it
 * advances while spin-waiting for each keystroke, so the value reflects how
 * long the user took to type. The host backend returns 0 so random() stays
 * deterministic (tests/integration/025_random asserts a fixed sequence). */
uint16_t platform_entropy(void);

/* Video-RAM byte under the text cursor, backing the REPL's blinking cursor.
 * NULL when the card firmware owns the cursor (80-col); Apple II backend only
 * (the host has its own terminal cursor). */
unsigned char *platform_cursor_cell(void);

/* Read an entire source file at `path` into `buf` (up to `max` bytes).
 * On success returns 0 and sets *out_len to the byte count. Nonzero on
 * failure: 1 = open failed (e.g. not found), 2 = file larger than `max`,
 * other = backend I/O error code. The Apple II backend uses ProDOS MLI
 * (no cc65 stdio — that would drag the formatting runtime, docs/contributing/CONSTRAINTS.md
 * rule 3); the host backend uses fopen/fread. */
uint8_t platform_read_file(const char *path, char *buf,
                           uint16_t max, uint16_t *out_len);

#if defined(WITH_80COL)
/* 80-column text. Switch the console between 40- and 80-column text.
 * `platform_text80()` is a no-op unless the relevant card is detected at
 * runtime: //e firmware 80-col when WITH_IIE is present, or II+ Videx on
 * the SWIFTSAT / II+ Runner builds. SWIFTIIP lite has no 80-col path and
 * stays 40-column. */
void platform_text80(void);
void platform_text40(void);
/* Current console width in columns (40 or 80). Backs htab's range check
 * (1..width) so the same builtin honours 1..80 once text80() is active.
 * Defined only where an htab builtin consumes it (SWIFTAUX today). */
unsigned char platform_text_width(void);
#if defined(WITH_SWIFTSAT)
/* Is a II+ Videx Videoterm present? Backs the SWIFTSAT REPL banner's 80-col
 * hint (SWIFTSAT does NOT auto-enter; the user opts in with text80()). */
unsigned char platform_videx_present(void);
#endif
/* Destructive backspace for the line editor: erase the character left of
 * the cursor, leaving the cursor on the blank. Width-aware — the 80-col
 * path goes through the card firmware cursor (COUT), the 40-col path
 * through cc65 conio. */
void platform_erase_char_left(void);
#endif

/* Called once at shutdown. */
void platform_shutdown(void);

/* Busy-wait roughly `ms` milliseconds, backing the `wait(_:)` builtin.
 * The classic Apple II CPU clock is a fixed ~1.0205 MHz, so the cc65
 * implementation loops the monitor ROM WAIT routine ($FCA8) — the standard
 * Apple II delay primitive — with a build-time-calibrated A value, no
 * runtime speed probe. Accuracy is approximate (loop overhead; accelerator
 * cards run it short). The host implementation is a no-op so test output
 * stays deterministic. (Because $FCA8 lives in motherboard ROM, which is
 * banked out under the Saturn XLC bank, wait() can only run from MAIN — one
 * reason it is not on the at-MAIN-ceiling SWIFTSAT.) */
void platform_wait_ms(uint16_t ms);

/* Sound a square-wave tone on the 1-bit speaker ($C030), backing the
 * `tone(_:_:)` builtin: `half_period` sets the delay between speaker toggles
 * (pitch — larger is lower) and `cycles` the number of full square-wave
 * periods (duration). Blocks for the whole tone. The cc65 implementation
 * toggles $C030 in a counted loop — stable pitch for a given half_period but
 * not cycle-calibrated, and short on accelerator cards / IIgs fast mode (the
 * same best-effort caveat as wait()); unlike wait() it touches no ROM, so it
 * needs no bank juggling. The host implementation is a no-op so test output
 * stays deterministic. Like wait(), it ships only on the Family B Runner — a
 * tone voices a compiled program, not the at-ceiling REPLs. */
void platform_tone(uint16_t half_period, uint16_t cycles);

#if defined(WITH_TESTLOG)
/* On-target test sweep (design doc 018), Runner only. testlog_begin(active)
 * arms (or disarms) the "FAIL"-token watcher over print output before a test
 * runs; testlog_failed() reports whether "FAIL" was seen, so the Runner can
 * append a PASS/FAIL verdict to the data disk's TESTLOG. */
void    testlog_begin(uint8_t active);
uint8_t testlog_failed(void);
#endif

#endif /* SWIFTII_PLATFORM_H */
