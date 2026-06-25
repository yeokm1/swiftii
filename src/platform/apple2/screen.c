/* Apple II console output.
 *
 * Routes platform_write* through cc65's <conio.h> (`cputc` / `cputs`).
 * conio writes directly to the text-screen video page using cc65's
 * own cursor tracker, which works even when no DOS or BASIC.SYSTEM is
 * loaded — unlike raw COUT ($FDED), which dispatches through the
 * ProDOS/DOS I/O hooks and traps to the monitor when those hooks
 * aren't set.
 *
 * Two cc65 apple2 quirks we work around:
 *
 *   1. `cputc('\r')` resets the column to 0 only; `cputc('\n')` advances
 *      the row only. For a full CR+LF we emit both. See docs/contributing/LESSONS.md
 *      2026-05-16 "cc65 apple2 conio splits CR and LF".
 *
 *   2. cc65's `newline` routine wraps CV (vertical cursor) from row 23
 *      back to row 0 — it does NOT scroll the screen. We intercept the
 *      newline path here AND the column-wrap path (`cputc` auto-wraps
 *      from column 39 to the next row, hitting the same CV wrap when
 *      that next row would be 24). Both paths memcpy rows 1..23 up one
 *      slot, clear row 23, and re-home the cursor. The Apple II text
 *      screen interleaves groups of 8 rows across $0400-$07FF, so a
 *      row-base lookup table handles the addressing.
 *
 * Compiled only by cc65 for the apple2 target.
 */
#include "../platform.h"
#include "osdetect.h"

#include <stdint.h>
#include <conio.h>

#define SCREEN_ROWS  24
#define SCREEN_COLS  40
#define SPACE_BYTE   0xA0  /* ' ' XOR $80 — cc65 stores char ^ $80 */

/* Row base addresses (start byte for each visible row 0..23).
 * Formula: $0400 + (r & 7) * $80 + (r >> 3) * 40 */
static unsigned char *row_base(unsigned char r) {
  return (unsigned char *)(uint16_t)
    (0x0400u + ((uint16_t)(r & 7) << 7) + ((uint16_t)(r >> 3) * SCREEN_COLS));
}

#if defined(WITH_GR_TEXTWIN)
/* Top row of the scrolling region. 0 (full screen) in text mode; gr()'s
 * mixed mode sets it to 20 so print() output scrolls only inside the
 * 4-line text window (rows 20-23) and never drags the 40x40 GR area
 * (rows 0-19) upward — left unrestricted, the shifted top text line
 * rendered as colour blocks along the bottom-left of the picture.
 * gr()/grFull()/text() (builtins_xlc.c) write it directly; scroll_up_one
 * reads it. A plain global, not a setter function, so on SWIFTSAT the
 * writes sit in the XLC cold bank and MAIN carries only this byte + the
 * read (a setter body in MAIN overflowed SWIFTSAT by 5 B). Gated
 * WITH_GR_TEXTWIN — the Compiler and the lite REPLs have no flag and stay
 * byte-identical (SCROLL_TOP is then a literal 0). */
unsigned char g_gr_scroll_top;
#define SCROLL_TOP g_gr_scroll_top
#else
#define SCROLL_TOP 0
#endif

static void scroll_up_one(void) {
  unsigned char r;
  unsigned char j;
  unsigned char *dst;
  unsigned char *src;

  for (r = SCROLL_TOP; r < SCREEN_ROWS - 1; ++r) {
    dst = row_base(r);
    src = row_base((unsigned char)(r + 1));
    for (j = 0; j < SCREEN_COLS; ++j) {
      dst[j] = src[j];
    }
  }
  dst = row_base((unsigned char)(SCREEN_ROWS - 1));
  for (j = 0; j < SCREEN_COLS; ++j) {
    dst[j] = SPACE_BYTE;
  }
}

/* 80-column output path (firmware-COUT). Two arms, one shared
 * per-character path, selected by WITH_IIE:
 *   - //e built-in 80-col card (WITH_IIE, "track A"): JSR $C300 enables the
 *     //e firmware and hooks CSW; card-presence probe = ProDOS MACHID ($BF98)
 *     bit 4.
 *   - II+ Videx Videoterm (!WITH_IIE, slot 3, "track B"): point CSW at $C300
 *     (PR#3) and let the firmware cold-init on the first COUT; card-presence
 *     probe = the slot-3 Pascal-1.1 signature ($C305=$38 / $C307=$18).
 *
 * The mode switch is cheap; the per-character path is the cost, and it is
 * unavoidably MAIN-resident and hot (it sits on the 40-col primary path).
 * We chose firmware-COUT over a hand-rolled page driver (design doc 013,
 * Q1 = //e firmware-COUT): once the firmware is hooked, COUT ($FDED)
 * dispatches through CSW ($36/$37) into ROM that does the column
 * interleave / cursor / scroll and renders lowercase natively. That
 * sidesteps cc65 cputc's $60..$7F uppercase fold (the reason
 * emit_native_high / the //+ inverse-letter substitution exist) — so in
 * 80-col mode we feed raw high-bit bytes to the firmware and skip the
 * 40-col direct-video machinery entirely.
 *
 * Width is a runtime flag (default 40). text80() flips it only after a
 * card-presence probe; the build flag decides whether this code exists
 * at all, the runtime probe decides whether it is used. */
#if defined(WITH_80COL)
static unsigned char s_width = 40;
static unsigned char s_cout_ch;

/* Feed one byte (bit 7 already set) to the firmware COUT vector. */
static void cout_char(unsigned char c) {
  s_cout_ch = c;
  __asm__("lda %v", s_cout_ch);
#if defined(WITH_IIE)
  __asm__("jsr $FDED");   /* COUT -> CSW -> //e 80-col firmware */
#else
  /* II+ Videx: SwiftII's interpreter executes its compiler code from
   * language-card RAM (bank 2); the Family B Compiler/Runner keep ProDOS's
   * MLI in that same language card. Either way COUT ($FDED) and the routines
   * it reaches live in motherboard ROM, which is banked OUT while LC RAM is
   * mapped over $D000-$FFFF — a bare `jsr $FDED` would run RAM and BRK. Bank
   * ROM in for the call ($C082, read-ROM), then restore read-RAM-bank-2
   * ($C080) — the exact wrap cc65's own conio uses around its ROM calls
   * (da65 of the lite startup: bit $C082 / jsr $FC58 / bit $C080). Bank 2 is
   * what both the interpreter's LC code AND ProDOS map there, so the restore
   * is correct for every II+ build. Proven on real HW. */
  __asm__("bit $C082");   /* read ROM  -> $FDED = COUT */
  __asm__("jsr $FDED");
  __asm__("bit $C080");   /* restore read RAM bank 2 (LC code / ProDOS MLI) */
#endif
}

#if defined(WITH_IIE)
/* Capability probe: ProDOS MACHID ($BF98) bit 4 = 80-col card present.
 * A card-presence check, not the untrustworthy $FBB3 model byte. */
static unsigned char has_80col_card(void) {
  return (unsigned char)((*(volatile unsigned char *)0xBF98) & 0x10);
}

void platform_text80(void) {
  if (!has_80col_card()) return;   /* wrong hardware -> no-op */
  __asm__("jsr $C300");            /* enable //e 80-col firmware + hook CSW */
  s_width = 80;
  clrscr();
}

void platform_text40(void) {
  __asm__("sta $C00C");            /* 80COL off: 40-column video */
  __asm__("sta $C000");            /* 80STORE off */
  /* The //e 80-col firmware widened the monitor text window to 80 columns
   * (WNDWDTH $21). cc65's clrscr() calls ROM HOME ($FC58), which homes and
   * clears *within that window* — so without this reset HOME lands the
   * cursor in the stale 80-col window and the 40-col prompt appears far to
   * the right / on the wrong row. Restoring WNDWDTH alone suffices: the
   * firmware leaves WNDLFT/WNDTOP/WNDBTM ($20/$22/$23) at their text
   * defaults (0/0/24). (Leave ALTCHAR alone: the //e 40-col REPL renders
   * lowercase via $E0-$FF screen codes, which need the alternate character
   * set the firmware enabled.) */
  *(volatile unsigned char *)0x0021u = 40;   /* WNDWDTH = 40 */
  s_width = 40;
  clrscr();
}

#else  /* II+ / Videx Videoterm (track B) */
/* Capability probe: the Videoterm slot-3 ROM carries the Pascal-1.1 firmware
 * signature in its $C300 dispatch — $C305=$38 (SEC) and $C307=$18 (CLC). Both
 * were confirmed present on a real Videx Videoterm (2026-06-01: a
 * monitor dump showed $C305=38, $C307=18), and they are the canonical
 * Pascal-1.1 invariants.
 *
 * We deliberately do NOT check $C30B: an earlier probe required $C30B=$01, but
 * that byte is a device-class/signature field that varies by ROM revision — it
 * was calibrated to izapple2's bundled 2.4 ROM and was the cause of SWIFTSAT
 * failing to enter 80-col on the real card (probe returned 0, text80() no-op'd,
 * while the no-probe standalone worked). A card-presence capability probe, not
 * the untrustworthy $FBB3 model byte. On a II+ the slot ROM reads directly at
 * $C300-$C3FF (no //e INTCXROM gating). See LESSONS 2026-06-01. */
static unsigned char has_videx(void) {
  if (*(volatile unsigned char *)0xC305u != 0x38) return 0;
  if (*(volatile unsigned char *)0xC307u != 0x18) return 0;
  return 1;
}

#if defined(WITH_SWIFTSAT)
/* Public Videoterm-presence probe. Backs the SWIFTSAT REPL banner's
 * "type text80()" hint — shown only when a card is actually detected.
 * SWIFTSAT-gated (its sole caller), so it stays off the other builds. */
unsigned char platform_videx_present(void) {
  return has_videx();
}
#endif

void platform_text80(void) {
  static unsigned char inited = 0;
  if (!has_videx()) return;        /* no card -> no-op */
  /* Activate the Videoterm via the conventional PR#3 path: point CSW
   * ($36/$37) at the firmware's $C300 entry. COUT ($FDED) is a `JMP (CSW)`,
   * so the next character output lands in $C300, whose `BIT $FFCB` dispatch
   * cold-starts the card (programs the MC6845 80x24, clears, switches the
   * display to the Videoterm, hooks CSW to its output handler) and then
   * prints. This is exactly what Applesoft `PR#3` does, and is what real II+
   * hardware needs.
   *
   * An earlier hand-rolled init (`CSW=$C307` + `BIT $C300` + `JSR $C800`),
   * reverse-engineered from izapple2's bundled "Videx Videoterm ROM 2.4.bin",
   * FAILED on real hardware: on the real 2.4 ROM `$C800` is
   * `LDA $06B8,X` — a screen-hole routine, NOT the cold-init — so output
   * routed to the card but the 6845 was never programmed (garbage screen).
   * The real ROM's init lives behind the $C300 `BIT $FFCB` dispatch; that
   * branch reads bit 6 of the *main-ROM* byte at $FFCB, which differs under
   * izapple2 — why `JSR $C300` inits on iron but appeared to fail in the
   * emulator. Proven on real HW. See docs/contributing/design/013-80col-text.md Q2.
   *
   * No ROM-wrap is needed here: $C300 + $36/$37 are slot ROM / zero page, not
   * the LC-banked $D000-$FFFF, and we don't call into the firmware yet. The
   * actual init fires on the first cout_char COUT, which IS ROM-wrapped
   * (`bit $C082` ... `bit $C080`) — so when the firmware's `BIT $FFCB` runs,
   * $FFCB reads real ROM, not the LC RAM bank-2 mapped there. */
  __asm__("lda #$00");
  __asm__("sta $36");              /* CSWL = $00 */
  __asm__("lda #$C3");
  __asm__("sta $37");              /* CSWH = $C3  -> CSW = $C300 (PR#3 entry) */
  /* AN0 ON: hand the display to the Videoterm via the Soft Video Switch. The
   * firmware cold-init sets this on the FIRST text80(), but platform_text40()
   * turns AN0 OFF on revert — so a LATER text80() (after text()) must set it
   * back ON itself, otherwise the card would receive output while the Apple
   * video stays on screen. Harmless on first entry (the firmware sets it too). */
  *(volatile unsigned char *)0xC059u = 0;   /* AN0 on: Videoterm to screen */
  s_width = 80;
  /* Clear + home the Videoterm. The FIRST text80() relies on the firmware's
   * cold-init (fired by the next COUT to $C300), which clears the card screen
   * for free — so we skip an explicit clear there. But once initialised the
   * firmware re-points CSW to its own output handler, so a LATER text80()
   * (after text()) re-sets CSW=$C300 and the next COUT takes the firmware's
   * OUTPUT path, not init — no clear — leaving new output in the middle of the
   * stale 80-col screen (the "text80 returns mid-screen" report).
   * So on re-entry emit the firmware clear-screen/home control (Ctrl-L) via
   * COUT. Confirmed clears+homes on the target Videoterm ROM. */
  if (inited) {
    cout_char(0x8C);               /* Ctrl-L ($0C | $80): clear screen + home */
  }
  inited = 1;
  /* No clrscr() on first entry: cc65 clrscr writes the Apple 40-col page
   * ($0400-$07FF) + ROM HOME, which the Videoterm does not display. */
}

#if defined(WITH_SWIFTSAT) || defined(WITH_SWB)
/* Videx 40-col revert backs the text() builtin on SWIFTSAT (XLC dispatch) and
 * the Family B II+ Runner (builtins_xlc.c normal-CODE dispatch). The lite II+
 * (SWIFTIIP) 80-col stretch has no text() builtin — it auto-enters 80-col at
 * init and stays there — so gating this off lite keeps the ~20 dead bytes off
 * its tight budget (cc65 links screen.o whole, so an ungated never-called
 * definition would bloat it). */
void platform_text40(void) {
  /* Restore standard 40-col Apple video output. Two steps, both needed —
   * confirmed on a real II+ + Videoterm (2026-06-14):
   *   1. AN0 OFF ($C058). The Videx Soft Video Switch follows annunciator 0:
   *      the firmware's $C300 cold-init set AN0 ON ($C059) to hand the display
   *      to the Videoterm, so AN0 OFF hands it back to the Apple video. PR#0
   *      (CSW) alone does NOT do this — it only stops *routing* COUT to the
   *      card; the Videoterm stayed visible without the AN0 reset (the
   *      "text() does not revert" real-HW report).
   *   2. PR#0 equivalent: point CSW back at COUT1 ($FDF0) so subsequent output
   *      goes to the 40-col Apple screen, not the card. */
  *(volatile unsigned char *)0xC058u = 0;      /* AN0 off: Apple video to screen */
  *(volatile unsigned char *)0x0036u = 0xF0;   /* CSWL = COUT1 ($FDF0) */
  *(volatile unsigned char *)0x0037u = 0xFD;   /* CSWH */
  s_width = 40;
  clrscr();
}
#endif
#endif /* WITH_IIE vs Videx */

/* Destructive backspace: erase the character left of the cursor and leave
 * the cursor on the blank. In 80-col mode the card firmware (//e built-in or
 * II+ Videx) owns the cursor — cc65's gotoxy/wherex track only the 40-col
 * monitor cursor — so route the erase through the firmware via COUT: BS
 * moves left, space overwrites + advances,
 * BS steps back onto the blank. The firmware also wraps to the previous
 * line at the left margin, which the 40-col gotoxy path could not. In 40-col
 * (s_width==40) it falls back to the cc65 gotoxy path.
 *
 * keyboard.c calls it on every WITH_80COL line-editor path, so a typed
 * backspace erases correctly in 80-col (it would otherwise use the cc65
 * 40-col erase, invisible on the card). Defined for all WITH_80COL builds;
 * SWIFTIIP (no WITH_80COL) uses keyboard.c's inline 40-col erase and stays
 * byte-identical. */
void platform_erase_char_left(void) {
  if (s_width == 80) {
    cout_char(0x88);               /* BS  ($08 | $80): cursor left      */
    cout_char(0xA0);               /* ' ' ($20 | $80): overwrite, advance*/
    cout_char(0x88);               /* BS: back onto the blank            */
    return;
  }
  {
    unsigned char x = wherex();
    unsigned char y = wherey();
    if (x > 0) {
      gotoxy((unsigned char)(x - 1), y);
      cputc(' ');
      gotoxy((unsigned char)(x - 1), y);
    }
  }
}

#if defined(WITH_SWIFTSAT) || defined(WITH_SWIFTAUX) || defined(WITH_SWB)
/* Current console width (40 or 80). Drives htab's upper bound; the htab
 * builtin's range check is 1..platform_text_width(). SWIFTAUX (//e aux)
 * and the //e Family B Runner have an htab builtin; the lite binaries
 * don't, so gate the accessor to keep the unused bytes off their budget.
 * (SWIFTSAT no longer builds WITH_80COL, so this resolves to SWIFTAUX +
 * the //e Runner.) */
unsigned char platform_text_width(void) {
  return s_width;
}
#endif
#endif /* WITH_80COL */

/* Video-RAM byte under the 40-col text cursor, for the REPL's hand-rolled
 * blinking cursor (keyboard.c). cc65 conio tracks the 40-col cursor in
 * wherex/wherey, and row_base maps a row to its interleaved page address, so
 * the next-write cell is row_base(y)+x. Returns NULL when the card firmware
 * owns the cursor (80-col): there the firmware blinks its own cursor, so the
 * caller lets cgetc block rather than draw a block over the invisible 40-col
 * page. (Defined outside the WITH_80COL guard — the 40-col-only binaries need
 * it too; the s_width test that returns NULL only exists where 80-col does.) */
/* Gated to the REPL interpreters whose read_key_blink actually blinks (not the
 * Family B Runner/Compiler — cc65/ld65 link whole object modules, so an
 * exported-but-unused definition would still cost their MAIN budget). On
 * SWIFTSAT it lives in the Saturn-bank-1 XLC overlay alongside its sole caller
 * read_key_blink; row_base / wherex / wherey stay in MAIN and are reachable
 * from bank 1. */
#if !defined(WITH_SWB)
#if defined(__CC65__) && defined(WITH_SWIFTSAT)
#pragma code-name (push, "XLC")
#endif
unsigned char *platform_cursor_cell(void) {
#if defined(WITH_80COL)
  if (s_width == 80) return (unsigned char *)0;
#endif
  return row_base(wherey()) + wherex();
}
#if defined(__CC65__) && defined(WITH_SWIFTSAT)
#pragma code-name (pop)
#endif
#endif

int platform_init(void) {
  osdetect_init();
  clrscr();
#if defined(WITH_80COL)
#if defined(WITH_IIE)
  /* Assert the alternate character set so lowercase renders. The //e
   * lowercase path (emit_native_high) writes screen codes $E0-$FF, which
   * are lowercase glyphs only when ALTCHAR is on; the //e RESET handler
   * powers up with it OFF. Entering 80-col (JSR $C300) turns it on as a
   * side effect, but a card-less //e never gets there and would otherwise
   * show lowercase as uppercase — so set it explicitly here, up front.
   * (II+ / Videx has no ALTCHAR — its glyphs come from the card's own
   * character ROM once the Videoterm is active.) */
  *(volatile unsigned char *)0xC00Fu = 0;   /* SETALTCHAR */
#endif
  /* Auto-enter 80-col at init when the card is present and the binary has
   * chosen that policy.
   * platform_text80() runs the card-presence probe (MACHID on //e, the slot-3
   * Videx signature on II+) and no-ops with no card, so a card-less //e or a
   * non-Videx II+ stays 40-col. The //e binaries (SWIFTIIE lite + SWIFTAUX)
   * auto-enter, and also expose text80()/text() for explicit switches. The
   * lite II+ (SWIFTIIP) is built without WITH_80COL and stays 40-col.
   *
   * SWIFTSAT (II+ Saturn) does NOT auto-enter — it stays default-40 and opts
   * in via text80() (the historical decision: the Videoterm's
   * default-on-everything was undesirable; opt-in is friendlier).
   *
   * The Family B Runner (WITH_SWB) does NOT auto-enter — on the //e its
   * `JSR $C300` 80-col-firmware enable left raw MLI file I/O (prodos.c)
   * returning $01 "bad call", and 80-col is a per-program opt-in (text80())
   * anyway. ALTCHAR above still gives the //e Runner lowercase.
   * Gated so the interpreters (no WITH_SWB) stay byte-identical. */
#if !defined(WITH_SWB) && !defined(WITH_SWIFTSAT)
  platform_text80();
#endif
#endif
  return 0;
}

static void emit_newline(void) {
  if (wherey() >= (unsigned char)(SCREEN_ROWS - 1)) {
    scroll_up_one();
    gotoxy(0, (unsigned char)(SCREEN_ROWS - 1));
  } else {
    cputc('\r');
    cputc('\n');
  }
}

static void emit_raw(char c) {
  unsigned char y_before;

  /* cc65's cputc writes the char first, then auto-advances the column.
   * When the column passes 39 it calls newline internally, and that
   * wraps CV from 23 to 0 — visually, the cursor "underruns" to the
   * top of the screen and subsequent writes clobber the banner. Detect
   * the row-23 -> row-0 transition and scroll: the char we just wrote
   * is on what was row 23 and the scroll lifts it to row 22, with the
   * cursor positioned at (0, 23) ready for the next char. */
  y_before = wherey();
  cputc(c);
  if (y_before == (unsigned char)(SCREEN_ROWS - 1) && wherey() == 0) {
    scroll_up_one();
    gotoxy(0, (unsigned char)(SCREEN_ROWS - 1));
  }
}

#ifndef WITH_IIE
static void emit_inverse_letter(char c) {
  /* c is an uppercase letter A-Z. cc65's apple2 conio stores `ch ^ $80`
   * for normal-video glyphs; inverse-video uses the screen codes $01..$1A
   * (bits 6 and 7 clear): 'A' ($41) - $40 = $01. We feed that byte
   * through cputc, which renders it inverse without us touching INVFLG. */
  unsigned char y_before;
  unsigned char inv;

  inv = (unsigned char)(c - 0x40);   /* 'A' (0x41) -> 0x01 */
#if defined(WITH_INVERSE_JM)
  /* 'J' -> $0A and 'M' -> $0D are the exceptions: cputc intercepts $0A/$0D as
   * LF/CR, so it would swallow those two letters — a stray CR mid-word wraps the
   * rest of the line back over itself (e.g. uppercase program output, the
   * Runner's "Running:" file-name echo, or the pre-IIe Compiler's "Compiling:" /
   * "Wrote:" path echo: ENAMELEN -> "ELEN" after the 'M' acts as a CR). Write
   * those two letters straight to video RAM and step the cursor exactly as cputc
   * would (mirrors emit_native_high on //e). Built by the Runner (uppercase
   * program output) AND the pre-IIe Compilers (uppercase ProDOS-path echo); the
   * at-ceiling REPLs leave it out and stay byte-identical. */
  if (inv == 0x0A || inv == 0x0D) {
    unsigned char x = wherex();
    unsigned char y = wherey();
    /* Match cputc's glyph transform so J/M get the SAME video as the letters
     * around them. cputc stores `(ch ^ $80) & INVFLG`; writing the raw inverse
     * code ($0A/$0D) instead drew J/M in the opposite polarity to their
     * neighbours (an inverse 'M' among normal-video letters). No SwiftII binary
     * calls revers(), so the monitor's INVFLG ($0032) stays $FF (normal video)
     * and the `& INVFLG` term is a no-op we can drop — leaving just `inv ^ $80`
     * (J -> $8A, M -> $8D = normal-video J/M), which keeps the at-budget II+
     * Compiler off its BSS ceiling. (If an inverse-text mode is ever added,
     * restore the `& *(volatile unsigned char *)0x0032` so J/M track it.) */
    row_base(y)[x] = (unsigned char)(inv ^ 0x80);
    if ((unsigned char)(x + 1) < SCREEN_COLS) {
      gotoxy((unsigned char)(x + 1), y);
    } else {
      emit_newline();
    }
    return;
  }
#endif
  y_before = wherey();
  cputc((char)inv);
  if (y_before == (unsigned char)(SCREEN_ROWS - 1) && wherey() == 0) {
    scroll_up_one();
    gotoxy(0, (unsigned char)(SCREEN_ROWS - 1));
  }
}
#endif /* !WITH_IIE */

#ifdef WITH_IIE
static void emit_native_high(char c) {
  /* //e-target builds (//e-disk lite + SWIFTAUX) only. cc65's apple2
   * cputc folds the byte range $60..$7F to uppercase (it does
   * `EOR #$80 / CMP #$E0 / AND #$DF` because the original Apple II
   * character ROM has no glyphs there). The //e DOES have them —
   * lowercase a-z plus `` ` { | } ~ `` live at screen codes $E0..$FF.
   * We render them by writing that screen code (the byte with bit 7
   * set) straight into video RAM, bypassing the fold, then advance the
   * cursor exactly as cputc would (column step, or a newline / scroll
   * at the right margin). Normal video only — the REPL never sets
   * inverse for output. The II+-disk binaries don't build this. */
  unsigned char x;
  unsigned char y;

  x = wherex();
  y = wherey();
  row_base(y)[x] = (unsigned char)((unsigned char)c | 0x80);
  if ((unsigned char)(x + 1) < SCREEN_COLS) {
    gotoxy((unsigned char)(x + 1), y);
  } else {
    emit_newline();
  }
}
#endif /* WITH_IIE */

#if defined(WITH_TESTLOG)
/* On-target test sweep (design doc 018): watch print output for the "FAIL"
 * token so the Runner can record a PASS/FAIL verdict per test. Only the Runner
 * builds with WITH_TESTLOG; every other binary that links screen.c stays
 * byte-identical. The self-checking tests print uppercase "FAIL" (via chk())
 * ONLY on a failed check; the tally line "fail 0" is lowercase, so it never
 * false-triggers. */
static uint8_t s_tl_active;   /* 1 while a test runs in sweep mode */
static uint8_t s_tl_match;    /* chars of "FAIL" matched so far     */
static uint8_t s_tl_failed;   /* 1 once "FAIL" has been seen        */
void testlog_begin(uint8_t active) {
  s_tl_active = active; s_tl_match = 0; s_tl_failed = 0;
}
uint8_t testlog_failed(void) { return s_tl_failed; }
#endif

static void emit(char c) {
#if defined(WITH_TESTLOG)
  if (s_tl_active) {
    static const char TOK[] = "FAIL";
    if (c == TOK[s_tl_match]) {
      if (++s_tl_match == 4) { s_tl_failed = 1; s_tl_match = 0; }
    } else {
      s_tl_match = (c == 'F') ? 1 : 0;
    }
  }
#endif
#if defined(WITH_80COL)
  /* In 80-col mode the card firmware (//e built-in or II+ Videx) owns the
   * cursor, interleave, scrolling and lowercase: send a CR for either
   * newline byte, otherwise the raw byte with bit 7 set. None of the
   * 40-col cputc/scroll machinery runs. */
  if (s_width == 80) {
    cout_char((c == 0x0A || c == 0x0D) ? 0x8D : (unsigned char)((unsigned char)c | 0x80));
    return;
  }
#endif

  if (c == 0x0A || c == 0x0D) {
    emit_newline();
    return;
  }

#ifdef WITH_IIE
  /* //e target: the keyboard and character ROM are full ASCII. cc65's
   * apple2 cputc still folds $60..$7F to uppercase, so render those
   * bytes (lowercase + `` ` { | } ~ ``) as $E0..$FF screen codes via a
   * direct video-RAM write. ($0A/$0D handled above.) No machine-type
   * probe — the disk build declares the //e (design doc 003 rev 4). */
  if ((unsigned char)c >= 0x60) {
    emit_native_high(c);
    return;
  }
#else
  /* Pre-IIe character ROM has glyphs for $20..$5F only. Canonical
   * SwiftII bytes that fall outside that range get substituted:
   *
   *   - lowercase letters a-z ($61..$7A): rendered as normal-video
   *     uppercase. Lowercase dominates Swift source, so the common case
   *     stays unhighlighted.
   *   - uppercase letters A-Z ($41..$5A): rendered as inverse-video
   *     uppercase. The user reads inverse-A as "uppercase A" and
   *     normal-A as "lowercase a", giving a visual case indicator.
   *   - `{` ($7B), `}` ($7D), `|` ($7C): no glyphs, so rendered as
   *     their input-method digraph forms (`<%`, `%>`, `??!`).
   *
   * Bytes stored in memory are unchanged; only the on-screen rendering
   * on pre-IIe machines differs. IIe / IIc / IIc+ / IIgs have a full
   * character ROM and pass through.
   * See docs/contributing/design/003-apple2-input-method.md revision 4. */
  if (APPLE_II_IS_PRE_IIE(platform_caps.machine_type)) {
    if (c >= 'a' && c <= 'z') { emit_raw((char)(c - 0x20)); return; }
    if (c >= 'A' && c <= 'Z') { emit_inverse_letter(c); return; }
    if (c == '{') { emit_raw('<'); emit_raw('%'); return; }
    if (c == '}') { emit_raw('%'); emit_raw('>'); return; }
    if (c == '|') { emit_raw('?'); emit_raw('?'); emit_raw('!'); return; }
  }
#endif
  emit_raw(c);
}

void platform_write(const char *buf, uint16_t len) {
  uint16_t i;
  for (i = 0; i < len; ++i) {
    emit(buf[i]);
  }
}

void platform_write_str(const char *s) {
  while (*s) {
    emit(*s++);
  }
}

void platform_putc(char c) {
  emit(c);
}

void platform_clear_screen(void) {
  /* cc65 clrscr() blanks the text page and homes the cursor at (0,0). */
  clrscr();
}

#if defined(WITH_SWIFTSAT) || defined(WITH_SWIFTAUX) || defined(WITH_SWB)
/* Cursor positioning backs the htab/vtab platform builtins (SWIFTSAT +
 * SWIFTAUX, the latter via the XLCPMEM copy-down group) and, under
 * WITH_SWB, the Family B Compiler's in-place percent progress line
 * (compiler_main print_pct rewrites column 1). Gated so the lite
 * binaries (no XLC, no WITH_SWB — no caller) stay byte-identical —
 * cc65 links whole object modules, so an ungated definition here would
 * bloat those binaries with two never-called functions. */
void platform_htab(uint8_t col) {
  /* 1-based column -> 0-based; preserve the current row. The caller
   * (xlc_htab_dispatch) has already range-checked 1..40 (or 1..80 in
   * 80-col mode). */
#if defined(WITH_80COL)
  if (s_width == 80) {
    /* In 80-col mode the cc65 conio cursor (40-col model) is bypassed —
     * output runs through the card firmware, whose horizontal cursor is
     * OURCH ($057B), 0-based 0..79. Set it directly; the firmware COUT path
     * reads it on the next character. */
    *(volatile unsigned char *)0x057Bu = (unsigned char)(col - 1);
    return;
  }
#endif
  gotoxy((unsigned char)(col - 1), wherey());
}

void platform_vtab(uint8_t row) {
  /* 1-based row -> 0-based; preserve the current column. The caller
   * (xlc_vtab_dispatch) has already range-checked 1..24. */
#if defined(WITH_80COL)
  if (s_width == 80) {
    /* Like htab, the cc65 conio cursor (40-col model) is bypassed in 80-col —
     * output runs through the card firmware. The firmware's vertical cursor is
     * the standard monitor CV ($25): unlike the column (which needs OURCH
     * because 0..79 overflows the 0..39 CH byte), rows are 0..23 in BOTH widths,
     * so no "OURCV" shadow is needed. */
    *(volatile unsigned char *)0x0025u = (unsigned char)(row - 1);
#if defined(WITH_IIE)
    /* //e built-in 80-col: the firmware CACHES the line base (BASL/BASH) and
     * only recomputes it on its own vertical moves, so a bare CV write scrambles
     * output onto the stale line (confirmed on real //e aux). Recompute the base
     * from CV via the monitor VTAB ($FC22 = LDA CV; BASCALC). ROM is reachable
     * bare here — cout_char calls $FDED without the LC wrap the II+ needs.
     * The Videx arm (!WITH_IIE) re-derives the address from CV per character
     * (its 6845 cursor), so the bare poke alone works there — confirmed on a
     * real II+ Videoterm — and must NOT run the 40-col BASCALC. */
    __asm__("jsr $FC22");
#endif
    return;
  }
#endif
  gotoxy(wherex(), (unsigned char)(row - 1));
}
#endif

void platform_shutdown(void) {
  /* `:quit` (and any REPL exit) returns to the boot selector instead of the
   * bare ProDOS dispatcher: cold-reboot -> ProDOS reloads -> auto-launches
   * the first `*.SYSTEM` (SWIFTII.SYSTEM, the boot menu). Never returns.
   *
   * Why a full reboot: the interpreter overwrites ProDOS's language-card RAM
   * with its own LC segment, so it CANNOT MLI-re-chain (the MLI body is
   * gone), and the ProDOS 2.4 "enhanced QUIT with pathname" trick crashes
   * 2.4.3 (Mariani + izapple2). A disk reboot is the reliable way back.
   *
   * The four instructions below are inlined (not a shared routine + `jsr`)
   * so the sequence costs the same handful of bytes on every binary —
   * notably SWIFTSAT, whose ~11 B MAIN headroom can't fit a routine plus a
   * call to it. cc65 drops the dead epilogue after the terminal `jmp`.
   *
   *   1. `bit $C082` banks the motherboard ROM into $D000-$FFFF — the exact
   *      idiom cout_char() uses on this build. While the interpreter runs,
   *      its LC RAM is mapped there, hiding the reset vector + monitor, so a
   *      naive `JMP $C600` runs interpreter RAM and dies in the "monitor"
   *      ($FF5A, observed 2026-06-02).
   *   2. Zero $3F4 (the power-up byte) so the autostart RESET handler does a
   *      COLD start: memory clear + slot scan + boot the disk.
   *   3. `jmp ($FFFC)` through the ROM reset vector -> autostart cold start.
   *
   * Slot- and version-independent; heavier than a QUIT (full reload) but
   * guaranteed. */
  __asm__("bit $C082");      /* read motherboard ROM into $D000-$FFFF */
  __asm__("lda #$00");
  __asm__("sta $03F4");      /* invalidate warm-start -> next RESET is COLD */
  __asm__("jmp ($FFFC)");    /* ROM reset -> cold start -> boot disk */
}

#if defined(WITH_FILE_CRUD)
/* Backs the wait() builtin, which on cc65 ships ONLY on the Family B Runner
 * (WITH_FILE_CRUD). Gating keeps every other binary that pulls in screen.c —
 * the lite REPLs, SWIFTSAT, SWIFTAUX, and the Family B Compiler (WITH_SWB but
 * not WITH_FILE_CRUD) — byte-identical (cc65 links whole object modules, so
 * an ungated definition would add this dead code to each). */
void platform_wait_ms(uint16_t ms) {
  /* The classic Apple II runs the 6502 at a fixed ~1.0205 MHz, so a cycle
   * count maps to a constant wall-clock time and no runtime calibration is
   * needed. The monitor ROM WAIT ($FCA8) burns 0.5*(5*A^2 + 27*A + 26)
   * cycles; A = $12 (18) is ~1010 cycles ~= 1 ms. Loop it `ms` times. The C
   * loop overhead makes this run a few percent long — within "approximate",
   * which is all wait() promises. An accelerator card (or IIgs fast mode)
   * runs the loop short; that's the documented best-effort caveat.
   *
   * Bare __asm__ statements (like platform_shutdown above) — WAIT only
   * clobbers A and returns with carry set + A = 0, leaving cc65's loop
   * counter (X/Y or zero page) intact.
   *
   * Millisecond grain is deliberate (pacing/animation). This is NOT a sound
   * path: a 1-bit speaker tone needs ~100-500 us half-period toggles of
   * $C030, finer than 1 ms, and the per-call VM dispatch overhead would jitter
   * any half-period a SwiftII loop could request — sound has to be one native
   * routine (tone(), shipped as a Phase 16 Family B builtin). The
   * hardware floor is one 6502 cycle (~1 us), so sub-microsecond units are
   * meaningless. */
  /* ROM BANKING (critical): $FCA8 is in the motherboard ROM ($F800-$FFFF),
   * but the Runner runs with ProDOS's MLI mapped over $D000-$FFFF (the
   * language-card RAM) — a bare `jsr $FCA8` lands in ProDOS RAM and crashes.
   * Bank the ROM in for the call (`bit $C082`, read-ROM) and restore
   * read-RAM-bank-2 (`bit $C080`, where ProDOS's MLI lives) after — the exact
   * wrap cout_char() uses around its firmware ROM calls (real-HW verified).
   * Per iteration, so ROM is mapped only across the JSR; cc65's loop control
   * runs from main RAM and is unaffected. */
  while (ms != 0) {
    __asm__("lda #$12");       /* 18 -> ~1 ms per ROM WAIT call */
    __asm__("bit $C082");      /* read motherboard ROM into $D000-$FFFF */
    __asm__("jsr $FCA8");      /* monitor WAIT */
    __asm__("bit $C080");      /* restore read RAM bank 2 (ProDOS MLI) */
    --ms;
  }
}

/* Backs the tone() builtin — same Family B Runner-only gating as
 * platform_wait_ms above. Sounds a square wave on the 1-bit speaker: every
 * access to $C030 flips the cone, so one full period is two edges (toggles)
 * separated by a `half_period` delay. We loop 2*cycles edges for `cycles`
 * full periods. A WRITE to $C030 toggles the speaker just like a read, and —
 * unlike a discarded volatile READ, which cc65 elides — the store is never
 * optimised away (the same reason poke(49200, 0) clicks). No ROM is touched
 * (contrast WAIT's $FCA8 wrap), so no bank juggling is needed. Timing is a
 * cc65 counted loop: stable for a given half_period but not cycle-calibrated,
 * and short on accelerators / IIgs fast mode — the documented best-effort
 * caveat. half_period 0 just toggles as fast as the loop allows (ultrasonic);
 * cycles 0 is silent. One delay loop (not one per edge) keeps the Runner BSS
 * footprint down. */
void platform_tone(uint16_t half_period, uint16_t cycles) {
  uint16_t edges = (uint16_t)(cycles << 1);   /* two edges per full period */
  while (edges != 0) {
    uint16_t d = half_period;
    *(volatile unsigned char *)0xC030 = 0;     /* flip the speaker cone */
    while (d != 0) { --d; }                     /* hold for half a period */
    --edges;
  }
}
#endif
