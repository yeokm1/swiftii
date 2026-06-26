/* Apple II keyboard input.
 *
 * Polls the keyboard through cc65's conio (`cgetc`) and echoes each
 * character locally so a REPL line shows up as the user types. Return
 * ($0D from the hardware) terminates the line and is stored as '\n' so
 * the lexer (which treats $0A and $0D identically as newlines) sees a
 * consistent terminator.
 *
 * Backspace / left-arrow ($08) deletes the last character with a
 * destructive `BS SPACE BS` echo sequence. The //e REPL binaries add
 * up/down (Ctrl-P/N) recall of recent input lines on top of that — see
 * the WITH_LINE_HISTORY block below (ROADMAP). Multi-line input
 * stays post-v1 (the in-launcher editor is its home).
 *
 * The stock Apple II Plus keyboard is uppercase-only and lacks
 * `{ } [ ] \ _ |`. On a pre-IIe machine, once the user hits Return the
 * buffered typed line is rewritten in place by `input_translate`
 * (design doc 003 rev 3): letters auto-lowercase, apostrophe case
 * markers resolve, Ctrl-W becomes `_`, and C-standard digraphs (`<%`
 * `%>` `<:` `:>` `??/` `??!`) collapse into their canonical single
 * bytes. The lexer and everything downstream therefore see canonical
 * lowercase Swift, byte-identical to host-authored files.
 *
 * The above is the pre-IIe / II+ model. A //e / //c / //c+ keyboard can
 * type both letter cases and every symbol directly, so in a **`WITH_IIE`
 * build** (the //e-target binaries — the //e-disk lite and SWIFTAUX, see
 * design doc 003 rev 4) the rewrite is friction, not help: we bypass
 * `input_translate` entirely, pass the typed bytes
 * through, and echo via the screen module (whose `WITH_IIE` path renders
 * real lowercase). The user types real Swift case (CAPS LOCK off; `let`
 * lowercase, `Int` shifted) — the lexer requires exact lowercase
 * keywords. This is a *build*-time split, not a runtime probe: the disk
 * declares the machine (we don't trust the $FBB3 ID — emulator presets
 * misreport it, see tools/apple2/boot_launcher/boot_launcher.c), so the II+-disk
 * binaries (no `WITH_IIE`) are byte-identical to the pre-slice-4 lite.
 */
#include "../platform.h"
#include "input.h"
#include "../../common/config.h"   /* SWIFTII_RANDOM */
#include "histring.h"              /* derives WITH_LINE_HISTORY; recall ring API */
#if defined(__CC65__) && defined(WITH_SWIFTSAT) && !defined(WITH_SWB)
#include "../../common/errors.h"
#include "../../vm/opcodes.h"
#endif

#include <stdint.h>
#include <conio.h>

#define KEY_RETURN     0x0D
#define KEY_BACKSPACE  0x08
#define KEY_DELETE     0x7F
#define KEY_CTRL_W     0x17  /* input-layer underscore (design doc 003); matches the editor */
#define KEY_CTRL_D     0x04  /* EOF — exit the REPL on an empty line */

#if defined(__CC65__) && defined(WITH_SWIFTSAT) && !defined(WITH_SWB)
extern swiftii_err_t __fastcall__ call_xlc_dispatch(uint8_t id);

/* Set by the SWIFTSAT XLC core-builtin dispatcher while a program is blocked
 * in readLine(). That call is already running with Saturn bank 1 selected, so
 * platform_read_line must not recurse through call_xlc_dispatch for each key. */
unsigned char g_read_line_in_xlc;
#endif

/* Line-history recall — //e REPL binaries only (ROADMAP). The recall ring +
 * nav logic lives in histring.{c,h}, which derives WITH_LINE_HISTORY from
 * WITH_IIE && !WITH_SWB: the two //e REPL interpreters (the //e-disk lite
 * SWIFTIIE and the //e-aux extras SWIFTAUX) have the file + BSS headroom. The
 * II+ REPLs (no WITH_IIE) sit at the ProDOS ceiling and the Family B
 * Compiler/Runner (WITH_SWB) don't want the ~1 KB ring in their budget-tight
 * BSS, so all of those stay byte-identical. It is a Family A REPL-interpreter
 * convenience, so the "Compiler is full" constraint doesn't apply (see
 * ROADMAP). */
#if defined(WITH_LINE_HISTORY)
/* //e arrow / emacs-style keys. The //e keyboard sends these directly;
 * the II+ has no up/down keys (which is partly why this is //e-only). */
#define KEY_UP     0x0B  /* up-arrow    (Ctrl-K) — older line */
#define KEY_DOWN   0x0A  /* down-arrow  (Ctrl-J) — newer line */
#define KEY_CTRL_P 0x10  /* previous    — older line          */
#define KEY_CTRL_N 0x0E  /* next        — newer line          */
#endif

#if SWIFTII_RANDOM
/* Keypress-timing entropy for seeding random(in:). The Apple II has no clock,
 * so — exactly as Applesoft's RND relied on the monitor's KEYIN incrementing
 * its $4E/$4F seed while polling — we tally spin iterations spent waiting for
 * each key. The count reflects the human's reaction time, so it varies run to
 * run. cc65's cgetc shows no cursor during its own wait (see editor.c), so
 * pre-spinning changes nothing the user sees. We poll the keyboard register
 * ($C000 bit 7) directly rather than kbhit() to avoid pulling extra conio into
 * the budget-tight Runner; cgetc still consumes the key and clears the strobe.
 * Gated to SWIFTII_RANDOM: only the Runner (and host) execute random(), so the
 * REPL binaries that lack it (SWIFTSAT / lite) stay byte-identical and don't
 * pay the code in their razor-thin MAIN budget. */
static uint16_t s_entropy = 0;

uint16_t platform_entropy(void) { return s_entropy; }
#endif

#if defined(WITH_LINE_HISTORY)
/* The recall ring + nav decision live in histring.c (host-tested); keyboard.c
 * owns only the screen-echo half of a line replace. */

/* Erase the character left of the cursor — the same destructive BS/space/BS
 * dance as the backspace branch, reused to rewind a whole line on recall. */
static void hist_erase_one(void) {
#if defined(WITH_80COL)
  platform_erase_char_left();
#else
  unsigned char x = wherex();
  unsigned char y = wherey();
  if (x > 0) {
    gotoxy((unsigned char)(x - 1), y);
    cputc(' ');
    gotoxy((unsigned char)(x - 1), y);
  }
#endif
}

/* Replace the on-screen line (cur_i chars) with NUL-terminated `src`, echoing
 * through platform_putc (every WITH_LINE_HISTORY build is WITH_IIE, so that is
 * the correct lowercase-aware echo path). Returns the new length in `buf`. */
static uint16_t hist_replace(char *buf, uint16_t cur_i, const char *src,
                             uint16_t max_len) {
  uint16_t n = 0;
  while (cur_i > 0) { --cur_i; hist_erase_one(); }
  while (src[n] != '\0' && (uint16_t)(n + 2) < max_len) {
    buf[n] = src[n];
    platform_putc(src[n]);
    ++n;
  }
  return n;
}
#endif /* WITH_LINE_HISTORY */

/* Blinking-cursor key wait — the 40-col REPL analogue of the //e firmware
 * cursor, the same scheme the editor's read_key_blink uses (editor.c). cc65's
 * cgetc draws no cursor during its own wait, so in 40-col text the REPL prompt
 * sat with no caret. Draw an inverse-space block ($20: it's in the inverse
 * range $00-$3F, so it shows white-on-black regardless of INVFLG) at the cursor
 * cell, wait on kb_ready(), toggle the block on a counter, restore the
 * underlying byte on keypress, then let cgetc consume the key (clearing the
 * strobe). row_base+x/y is always a blank next-write cell in line input, so the
 * restore byte is whatever the grid had ($A0 space).
 *
 * The wait spins on kb_ready() — a real (non-inlined) function call wrapping the
 * $C000 read — rather than an inline register poll, so each idle pass costs one
 * call + the counter, the same shape as the editor's kbhit() and the launcher
 * file-selector's a_kbd() loops. With the shared 0x1FFF cadence that makes all
 * three cursors blink at the same rate (an inline poll iterates far faster and
 * blinked visibly quicker). We use kb_ready() rather than cc65's kbhit() so the
 * probe sits in this XLC region on SWIFTSAT (kbhit's conio body would land in
 * MAIN, which is at the budget wall); it reads $C000 with no ROM call, so it is
 * safe from the SWIFTSAT bank-1 excursion.
 * platform_cursor_cell returns NULL when the card firmware owns the cursor
 * (80-col): there cgetc must block so the firmware blinks its own cursor — a
 * pre-spin would hand cgetc an already-pending key and the firmware cursor
 * would never appear.
 *
 * A REPL-interpreter feature; gated OFF (call site below keeps the prior inline
 * cgetc, byte-identical) for WITH_SWB (Family B Runner/Compiler): not REPLs —
 * the Runner only runs compiled programs, and its BSS = heap + program image is
 * at its ceiling, so the blink's -Cl static locals would force a heap trim
 * (less runtime data for every program) to buy a cursor that only shows during
 * a program's readLine.
 *
 * On SWIFTSAT, only this key-wait helper lives in XLC and returns one byte at
 * a time to the MAIN line editor. Running the whole line editor under XLC left
 * the compiler's LC window unstable on return after REPL input. */
#if !defined(WITH_SWB)
#define CURSOR_BLOCK   0x20    /* inverse space = solid block */
#define BLINK_MASK     0x1FFFu /* toggle cadence; matches the editor + launcher */

#if defined(__CC65__) && defined(WITH_SWIFTSAT)
#pragma code-name (push, "XLC")
#endif

/* Non-blocking keyboard probe. A standalone call (cc65 does not inline it) so
 * the blink loop pays one call per idle pass — matching the editor's kbhit() and
 * the launcher's a_kbd() so all three blink at the BLINK_MASK rate. Direct $C000
 * read, no ROM, safe from the SWIFTSAT bank-1 excursion. */
static unsigned char kb_ready(void) {
  return (unsigned char)(*(volatile unsigned char *)0xC000 & 0x80);
}

#if defined(WITH_IIE) && defined(WITH_80COL)
/* //e 80-col cursor blink. The //e firmware's OWN input caret is screen code
 * $60 — a flashing space in the primary character set, i.e. a normal-looking
 * cursor. But the //e build runs ALTCHAR on so output renders lowercase, and
 * under ALTCHAR $60 is the backtick glyph: the firmware caret then shows as a
 * stray steady `. So draw our own block here instead, the 80-col analogue of the
 * 40-col path below: CURSOR_BLOCK is $20 (inverse space), in the $00-$3F inverse
 * range that ALTCHAR leaves alone, so it stays a clean white block. The cell is
 * the firmware's current line base (BASL/BASH) + (OURCH>>1), in AUX for an even
 * column and MAIN for an odd one; 80STORE + PAGE2 bank the $0400-$07FF store
 * (mirrors editor.c's vid_poke80, and touches only the text page, not zp/stack).
 * We blink until a key is ready, then cgetc reads it — already pending, it
 * returns at once, so the firmware never gets to draw its own $60 caret. (The
 * II+ Videx path keeps plain cgetc: its caret comes from the card's character
 * ROM as a clean block and there is no ALTCHAR to remap it.) */
static unsigned char read_key_blink_80iie(void) {
  uint16_t base = (uint16_t)(*(volatile unsigned char *)0x28)
                | ((uint16_t)(*(volatile unsigned char *)0x29) << 8);  /* BASL/BASH */
  unsigned char col = *(volatile unsigned char *)0x057B;               /* OURCH    */
  volatile unsigned char *cell =
      (volatile unsigned char *)(uint16_t)(base + (col >> 1));
  unsigned char save, on = 1;
  uint16_t t = 0;
  __asm__("sta $C001");                /* 80STORE on: PAGE2 banks the text page */
  if (col & 1) __asm__("sta $C054");   /* odd column  -> MAIN                   */
  else         __asm__("sta $C055");   /* even column -> AUX                    */
  save = *cell;
  *cell = CURSOR_BLOCK;
  while (!kb_ready()) {
    if ((uint16_t)(++t & BLINK_MASK) == 0) {
      on = (unsigned char)!on;
      *cell = on ? CURSOR_BLOCK : save;
    }
  }
  *cell = save;
  __asm__("sta $C054");                /* PAGE2 -> MAIN (restore)               */
  return (unsigned char)cgetc();
}
#endif

static unsigned char read_key_blink(void) {
  unsigned char *cur = platform_cursor_cell();
  unsigned char save;
  unsigned char on = 1;
  uint16_t t = 0;
  if (cur == (unsigned char *)0) {     /* 80-col: the card firmware owns the cursor */
#if defined(WITH_IIE) && defined(WITH_80COL)
    return read_key_blink_80iie();     /* //e: own block, not the ALTCHAR-`$60` caret */
#else
    return (unsigned char)cgetc();     /* II+ Videx: clean firmware block            */
#endif
  }
  save = *cur;
  *cur = CURSOR_BLOCK;
  while (!kb_ready()) {
    if ((uint16_t)(++t & BLINK_MASK) == 0) {
      on = (unsigned char)!on;
      *cur = on ? CURSOR_BLOCK : save;
    }
  }
  *cur = save;
  return (unsigned char)cgetc();
}

#if defined(__CC65__) && defined(WITH_SWIFTSAT)
swiftii_err_t xlc_repl_key_dispatch(uint8_t argc) {
  (void)argc;
  return (swiftii_err_t)read_key_blink();
}

#pragma code-name (pop)
#endif
#endif

int16_t platform_read_line(char *buf, uint16_t max_len) {
  uint16_t i;
  unsigned char c;
#if defined(WITH_LINE_HISTORY)
  /* Steps back from the live line: 0 = the line being typed, 1 = newest
   * history entry, ... up to s_ring.count = oldest. Reset every call. */
  uint8_t nav = 0;
#endif

  if (max_len < 2) return 0;

  i = 0;
  for (;;) {
#if !defined(WITH_SWB) && defined(WITH_SWIFTSAT)
    if (g_read_line_in_xlc) {
      c = (unsigned char)cgetc();
    } else {
      c = (unsigned char)call_xlc_dispatch(XLC_OP_REPL_KEY);
    }
#elif !defined(WITH_SWB)
    /* REPL interpreters: wait for a key while blinking the 40-col cursor (80-col
     * defers to the firmware cursor inside the helper). */
    c = read_key_blink();
#else
#if SWIFTII_RANDOM
    /* Spin until a key is ready, tallying the wait -> entropy. */
    while ((*(volatile unsigned char *)0xC000 & 0x80) == 0) ++s_entropy;
#endif
    c = (unsigned char)cgetc();
#endif

    if (c == KEY_RETURN) {
#ifndef WITH_IIE
      /* Rewrite the typed bytes in place into canonical lowercase
       * Swift before handing the line back to the caller. The
       * translation never grows the buffer, so writing back into
       * `buf` with the same `max_len` ceiling is safe.
       * (WITH_IIE: //e keyboard is already canonical -> no rewrite.) */
      i = input_translate(buf, i, buf, (uint16_t)(max_len - 2));
#endif
#if defined(WITH_LINE_HISTORY)
      /* Record the canonical line (//e keyboard is already canonical, so this
       * is the bytes the lexer will see) before terminating it. */
      histring_add(buf, i);
#endif
      buf[i] = '\n';
      ++i;
      buf[i] = '\0';
      /* Route the echo through platform_putc so the screen module's
       * scroll-on-newline logic runs (see screen.c emit_newline). */
      platform_putc('\n');
      return (int16_t)i;
    }

    if (c == KEY_BACKSPACE || c == KEY_DELETE) {
      /* The //+ left-arrow key emits $08, so this is the natural
       * "delete last char" binding. cc65's cputc treats $08 as a
       * printable glyph (writes screen byte, advances column) rather
       * than a cursor-move, so we have to move the cursor ourselves. */
#if defined(WITH_80COL)
      /* 80-col builds (//e): erase is width-aware — in 80-col the //e card
       * firmware owns the cursor, so the cc65 gotoxy path below would move
       * the wrong one (and write the invisible 40-col page). In 40-col,
       * platform_erase_char_left falls back to the same gotoxy path. */
      if (i > 0) {
        --i;
        platform_erase_char_left();
      }
#else
      /* gotoxy / wherex on the 40-col-only binaries (byte-identical). */
      if (i > 0) {
        unsigned char x;
        unsigned char y;
        --i;
        x = wherex();
        y = wherey();
        if (x > 0) {
          gotoxy((unsigned char)(x - 1), y);
          cputc(' ');
          gotoxy((unsigned char)(x - 1), y);
        }
      }
#endif
      continue;
    }

#if defined(WITH_LINE_HISTORY)
    if (c == KEY_UP || c == KEY_CTRL_P) {
      /* Walk to an older line. histring_older parks the in-progress line on the
       * first step away so down-arrow can bring it back; it returns NULL (no
       * redraw) once we're already at the oldest entry. */
      const char *src = histring_older(&nav, buf, i);
      if (src) i = hist_replace(buf, i, src, max_len);
      continue;
    }
    if (c == KEY_DOWN || c == KEY_CTRL_N) {
      /* Walk to a newer line; histring_newer hands back the parked in-progress
       * line when it steps below the newest, and NULL once already on it. */
      const char *src = histring_newer(&nav);
      if (src) i = hist_replace(buf, i, src, max_len);
      continue;
    }
#endif

#ifndef WITH_IIE
    /* Ctrl-W is the input-layer underscore key — the II+ keyboard has no `_`,
     * and this is the SAME key the editor uses, so the REPL and editor match.
     * Preserve it in the buffer so input_translate sees it, and echo `_`
     * locally for feedback. (Not built for WITH_IIE: on a //e the real `_`
     * key works, so $17 falls through to the control-skip.) */
    if (c == KEY_CTRL_W) {
      if ((uint16_t)(i + 2) >= max_len) continue;
      buf[i] = (char)c;
      ++i;
#if defined(WITH_80COL)
      platform_putc('_');  /* 80-col: via firmware COUT so it shows on the card */
#else
      cputc('_');
#endif
      continue;
    }
#endif

    /* Ctrl-D on an empty line exits the REPL (returns EOF, so repl_run's
     * `len <= 0` break fires — same exit as `:quit`). This matches the host,
     * whose terminal turns Ctrl-D into stdin EOF. Mid-line it falls through
     * to the control-char skip below (a non-empty line is never discarded).
     * Applies to all four Apple II binaries (shared keyboard.c). */
    if (c == KEY_CTRL_D && i == 0) return 0;

#if defined(WITH_SWB) && !defined(WITH_BIGLANG)
    /* Runner (Family B, not the WITH_BIGLANG Compiler): Ctrl-C aborts a
     * program blocked here, parity with the OP_LOOP poll a readLine wait never
     * reaches. cgetc has cleared the strobe; vm.c's inline readLine body turns
     * this sentinel into SE_BREAK. The Compiler reads source from disk (no
     * interactive readLine), so excluding it keeps its binary byte-identical. */
    if (c == 0x03) return PLATFORM_READ_BREAK;
#endif

    /* Skip other control characters; tabs aren't meaningful here. */
    if (c < 0x20) continue;

    /* Reserve space for '\n' + '\0'. */
    if ((uint16_t)(i + 2) >= max_len) continue;

    buf[i] = (char)c;
    ++i;
#if defined(WITH_IIE) || defined(WITH_80COL)
    /* Echo through the screen module (not raw cputc) so that:
     *   - 80-col mode renders on the card — the //e firmware owns the screen
     *     and cputc writes the invisible 40-col page;
     *   - the //e renders typed lowercase / `{ | }` correctly (cc65's apple2
     *     cputc folds $60..$7F to uppercase; emit() has the direct-write path).
     * In 40-col it is equivalent to cputc for the uppercase/symbol bytes a //+
     * keyboard can produce. The lite II+ binary (SWIFTIIP) has no WITH_80COL,
     * so it keeps plain cputc and stays byte-identical. */
    platform_putc((char)c);
#else
    cputc((char)c);
#endif
  }
}
