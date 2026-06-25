/* The on-target text editor (design doc 006).
 *
 * Since the merge the editor is NOT a standalone SYS binary: it is a
 * subsystem of the boot launcher (one SWIFTII.SYSTEM). The launcher's file
 * browser calls editor_main(path) in-process (see editor.h) and it returns on
 * Ctrl-Q — no cold reboot, because the launcher and the editor both keep ProDOS
 * MLI resident (MAIN-only, no language card mapped). A stock Apple II user thus
 * authors, saves, and runs `.swift` programs and bounces between the file list
 * and the editor without rebooting. It links the editor modules (gap buffer,
 * line navigation, screen render, key dispatch) plus input.c — NOT the
 * lexer/compiler/VM.
 *
 * This file is the platform loop: read a key, dispatch it
 * (keymap.c does the buffer/cursor work), render the frame (screen.c builds
 * the pure grid), and blit the grid straight to 40-col video RAM. It opens the
 * launcher-supplied file (or an empty scratch buffer); Ctrl-S/O save/open,
 * Ctrl-Q returns to the file browser. Ctrl-R saves the current cursor state
 * and returns an editor-run request to the launcher. (80-column rendering also
 * lands in a later slice.)
 *
 * Two responsiveness measures (4b follow-up):
 *   - Dirty-row blit: only the work/status/message rows that changed since
 *     the last frame are repainted, so a keystroke repaints ~1 row instead
 *     of all 24 (each cell goes through emit, which is not cheap on a 1 MHz
 *     6502).
 *   - Blinking block cursor: cc65's cgetc shows no cursor, so the key wait
 *     flashes an inverse block at the cursor position itself, polling the
 *     keyboard (kbhit) so input latency stays low regardless of blink rate.
 *
 * Display model: on a //+ build in cooked (digraph) mode typed letters are
 * auto-lowercased (the //+ keyboard is uppercase-only) so the buffer holds
 * canonical lowercase, drawn as normal-video uppercase (capitals show inverse)
 * — identical to a loaded file and to the launcher's file preview, and to what
 * is saved. Uppercase is entered with
 * the `'` case marker and the `<%`-style digraphs for braces; those (and any
 * `'`) are resolved to their canonical bytes at save by input_translate. The
 * grid is written straight to video RAM as Apple screen codes (see screen_code)
 * rather than through cc65 cputc. The //e launcher build (LITE_IIE) renders
 * native and types canonical directly (no auto-lowercase).
 *
 * Raw mode (Ctrl-G on a //+ build, see s_cooked): turns the whole Swift typing
 * model off — typed bytes are stored, displayed, and saved as-is (no
 * auto-lowercase, no inverse-case swap, no digraph expansion, no
 * canonicalisation). The default is per-file by extension (.swift -> cooked,
 * else raw); Ctrl-G reloads the file in the other mode so the buffer always
 * matches the active mode. On real //+ hardware raw mode shows lowercase /
 * `{ } |` through the limited character ROM (they read as their uppercase /
 * absent glyphs) — that is the accepted "as-is" view.
 */
#include "editor.h"
#include "gapbuf.h"
#include "screen.h"
#include "textnav.h"
#include "keymap.h"
#include "fileio.h"
#include "../platform/platform.h"
#include "../common/config.h"

#include <conio.h>
#include <string.h>

/* 80-column editing. The editor is linked into the boot
 * launcher, so it shares the launcher's sticky width flag and its 80-col-aware
 * ROM-COUT primitives. At 40 columns the editor blits straight to the 40-col
 * text page (fast, and the only way to get the //+ inverse-video glyphs);
 * at 80 columns that page isn't displayed, so it draws through the firmware via
 * a_cout / a_vtab instead (g_width80 selects the path). */
#ifdef __CC65__
extern unsigned char g_width80;                 /* launcher: 0 = 40-col, 1 = 80 */
extern void __fastcall__ a_cout(unsigned char c);     /* ROM COUT, OR $80 in   */
extern void __fastcall__ a_vtab(unsigned char row);   /* cursor to (col 0, row) */
extern void __fastcall__ a_home(void);                /* clear + home (width-aware) */
extern void ed_width(unsigned char on);         /* launcher: switch 40/80 in-session */
#define OURCH    (*(volatile unsigned char *)0x057B)  /* 80-col firmware column */
#define ED_W80   (g_width80)
/* At 80-col the editor draws through ROM COUT, which auto-wraps the cursor to
 * column 0 of the NEXT row once a glyph is printed in the rightmost (80th)
 * column — and the //e firmware leaves its inverse cursor block at that
 * wrapped-to cell. When the dirty-row diff doesn't repaint that next row, the
 * stray block survives and accumulates into a growing white bar down the left
 * edge (40-col writes video RAM directly, so it never hits this). So reserve
 * the last column: render and blit 79 columns, never the 80th, exactly as the
 * launcher's 80-col preview does. */
#define ED_COLS_CUR (g_width80 ? (ED_COLS_MAX - 1) : ED_COLS)
/* cc65 ProDOS file I/O leaves the language card banked to read RAM; at 80-col
 * the editor draws through ROM COUT, so re-bank motherboard ROM after any file
 * op (harmless at 40-col, where it draws to video RAM directly). */
static void ed_rebank_rom(void) { __asm__("bit $C082"); }
#else
#define ED_W80   0
#define ED_COLS_CUR ED_COLS
static void ed_rebank_rom(void) {}
#endif

/* Main-RAM text region. Since the merge the editor is part of the
 * boot launcher (one SYS binary), so its BSS starts ~9 KB higher than the old
 * standalone editor's and a 16 KB gap buffer up there would blow the $BF00
 * ceiling. Instead s_text (and the dirty-diff baseline s_prev, see below) are
 * placed (via #pragma bss-name) in the low-RAM GAPBUF region the launcher's
 * idle file browser otherwise uses for its directory block / entry tables
 * ($0800-$1BFF) — the two are strictly time-disjoint (editor_main runs only
 * while file_browser is blocked, and the browser rebuilds those tables on
 * return). The region is $1400 = 5120 B, ending at $1BFF just below the
 * launcher's MLI I/O buffer at $1C00. s_text + s_prev share it, so the gap
 * buffer gets whatever the EditorScreen doesn't take: on the II+ build the
 * frame is 40 cols wide (ED_COLS_MAX, screen.h) = 963 B, leaving 4 KB for
 * text — the same size as the Family B Compiler's streaming source window
 * (doc 016); the //e build keeps the 80-col frame (1,923 B) and a 3 KB gap
 * buffer. Both exceed the Family A 2 KB staged-run cap; only the Family B
 * Compiler (disk-streamed source) can run everything the editor can hold.
 * INVARIANT: GAPBUF_CAP + sizeof(EditorScreen) must fit the GAPBUF region size
 * in tools/apple2/boot_launcher/boot_launcher.cfg and must not push past $1C00
 * (ld65 errors on overflow, so a bad pairing cannot link). */
#if ED_COLS_MAX == 40
#define GAPBUF_CAP 0x1000
#else
#define GAPBUF_CAP 0x0C00
#endif

/* Pre-IIe rendering/typing model (inverse-video capitals, //+ auto-lowercase,
 * input_translate on save). True on the II+ disk's launcher build, false on
 * the //e disk's (-DLITE_IIE). */
#ifdef LITE_IIE
#define ED_PRE_IIE 0
#else
#define ED_PRE_IIE 1
#endif

/* Idle message-line legend (the key commands the cursor/edit keys don't make
 * obvious). `^` = Ctrl; fits the visible width in plain uppercase. ^T/^V page
 * the view up/down; ^W toggles text width 40<->80 (the 80-col fallback) — //e
 * disk only, since the II+ disk has no 80-col path. QUIT is NOT here: it's
 * pinned to the extreme right of the message row (stamp_quit_hint), always
 * visible. On //e the line is width-aware: at 80 cols everything is spelled
 * out, at 40 cols WIDTH shrinks to the bare ^W key and PAGE to ^TV PG. */
#if defined(LITE_IIE) || !defined(__CC65__)
#define ED_HELP_MSG \
  (ED_W80 ? "^S SAVE ^R RUN ^W WIDTH ^T/^V PAGE" \
          : "^S SAVE ^R RUN ^W WIDTH ^T/^V PG")
#else
#define ED_HELP_MSG "^S SAVE ^R RUN ^T/^V PG"
#endif

/* Cursor blink cadence. The wait toggles the cursor every BLINK_MASK+1
 * keyboard polls, the SAME mechanism and value the boot launcher's file
 * selector uses (read_name in boot_launcher.c, `& 0x1FFF`), so the two
 * cursors blink at the same Applesoft-like rate. One kbhit per loop pass,
 * matching the launcher's one a_kbd per pass. A key press breaks out
 * immediately, so this sets blink cadence, not input latency. */
#define BLINK_MASK 0x1FFFu

/* Place the gap buffer in the launcher's low-RAM GAPBUF segment ($0800), not
 * in the merged binary's high BSS (which would overflow $BF00). See the
 * GAPBUF_CAP note above and boot_launcher.cfg. The pragma is cc65-only; on the
 * host it's a plain BSS array. */
#ifdef __CC65__
#pragma bss-name(push, "GAPBUF")
#endif
static uint8_t s_text[GAPBUF_CAP];
/* The dirty-diff baseline (a full previous frame) lives here too, NOT in high
 * BSS: at ED_COLS_MAX=80 two EditorScreens (~1.9 KB each) in BSS left cc65's
 * file-I/O heap too small for the 1 KB page-aligned ProDOS I/O buffer open()
 * mallocs, so editor_file_load/save failed ("COULD NOT OPEN FILE"). Parking
 * s_prev in the low-RAM GAPBUF window (just past s_text) frees that heap; it is
 * editor-only, so reusing the browser's idle table RAM is safe (time-disjoint,
 * as for s_text). */
static EditorScreen s_prev;   /* last-blitted frame, for dirty-row diffing   */
#ifdef __CC65__
#pragma bss-name(pop)
#endif
static GapBuf s_gb;
static EditorState s_state;
static EditorScreen s_screen; /* ~1.9 KB — too big for the C stack, keep static */
static char s_fname[64];      /* current file (empty = untitled/scratch)     */
/* 1 = "cooked" (digraph) mode, 0 = "raw" mode. In cooked mode the pre-IIe
 * Swift typing model is in force: typed letters auto-lowercase (entry), the
 * inverse-video case swap (screen_code) + digraph expansion (screen.c) apply
 * (display), and input_translate/untranslate canonicalise on save/load
 * (fileio). Raw mode turns all of that off — bytes are typed, shown, and saved
 * as-is. The default is set at each load from the extension (.swift -> cooked,
 * anything else -> raw, mirroring the old editor_path_is_swift behaviour) and
 * the //+ user flips it with Ctrl-G (ED_PRE_IIE only; the //e disk has no
 * digraph model and uses Ctrl-W for its 80-col fallback — Ctrl-W is the
 * input-method `_` key here, and Ctrl-L is move-down, so the mode toggle is
 * Ctrl-G). NOT
 * re-derived per frame any more, so a toggle sticks. On a //e build
 * (ED_PRE_IIE 0) it is only the load/save canonicalisation selector and stays
 * at its extension default. */
static uint8_t s_cooked = 1;
static uint16_t s_run_cursor; /* cursor byte offset captured at Ctrl-R (run)  */
/* Long-line display mode (ED_MODE_*). WRAP: long lines flow onto continuation
 * rows so nothing is hidden off the right edge. (HSCROLL + an in-editor toggle
 * + an on-disk preference were considered and dropped 2026-06-06 — the editor
 * is the same on both disks, word-wrap only. ED_MODE_HSCROLL is unused.) */
static uint8_t s_linemode = ED_MODE_WRAP;

/* Idle message-line text. On a //+ build (ED_PRE_IIE) it advertises the Apple
 * Pascal `^O/L UP/DN` cursor keys and the `^G MODE` cooked/raw toggle, in place
 * of the static page hint (the arrows and Ctrl-T/V still page; they are just no
 * longer advertised here). The mode it currently holds is shown separately by
 * stamp_mode_tag on the status row. On the //e/host builds it is the plain
 * ED_HELP_MSG. (One static string, not a per-mode pair — BSS budget is tight.) */
static const char *idle_help(void) {
#if ED_PRE_IIE
  return "^O/L UP/DN ^S SAVE ^R RUN ^G MODE";
#else
  return ED_HELP_MSG;
#endif
}

/* Apple II text page-1 base address for a screen row.
 * Formula: $0400 + (r & 7) * $80 + (r >> 3) * 40. */
static uint8_t *vid_row(uint8_t r) {
  return (uint8_t *)(uint16_t)
    (0x0400u + ((uint16_t)(r & 7) << 7) + ((uint16_t)(r >> 3) * ED_COLS));
}

/* Translate a grid glyph (plain ASCII, already digraph-expanded by screen.c)
 * to its Apple text screen code, then write it straight to video RAM. On a
 * pre-IIe build uppercase A-Z render as inverse-video uppercase and lowercase
 * a-z as normal-video uppercase (design doc 003); everything else is normal
 * video.
 *
 * Why direct video writes instead of cputc: cc65's cputc treats $0A/$0D as
 * LF/CR — and those are exactly the screen codes the old inverse trick fed it
 * for 'j' ($6A-$60=$0A) and 'm' ($6D-$60=$0D). So a cputc-blitted line with a
 * 'j'/'m' garbled (the byte moved the cursor instead of being drawn), and the
 * blinking cursor block could not be ERASED when it sat on such a cell — it
 * stayed put as a stray second cursor. Writing the screen code ourselves
 * sidesteps cputc's control-code handling entirely, never scrolls, and makes
 * the cursor draw/erase deterministic. */
static uint8_t screen_code(uint8_t g) {
#if ED_PRE_IIE
  /* The inverse-video case swap is a Swift-authoring aid (tells case apart on a
   * machine with no lowercase glyphs); raw mode renders natively, so an all-caps
   * file (the II+ README) reads as plain normal-video text. */
  if (s_cooked) {
    if (g >= 'a' && g <= 'z') return (uint8_t)((g - 0x20) | 0x80); /* normal A-Z */
    if (g >= 'A' && g <= 'Z') return (uint8_t)(g - 0x40);          /* inverse A-Z ($01-$1A) */
  }
#endif
  return (uint8_t)(g | 0x80);                            /* normal video */
}

/* Paint one grid row straight to 40-col video RAM (a direct write never
 * scrolls, so the bottom-right cell needs no special case). */
static void blit_row(uint8_t r, const uint8_t *cells) {
  uint8_t *v = vid_row(r);
  uint8_t c;
  for (c = 0; c < ED_COLS; ++c) v[c] = screen_code(cells[c]);
}

#ifdef __CC65__
/* 80-col: draw the row through the firmware (ROM COUT via a_cout), which owns
 * the 80-col display. The grid holds canonical bytes and the //e firmware
 * renders lowercase natively (ALTCHAR), so no inverse mapping. `cols` is the
 * reserved 79-column width (see ED_COLS_CUR): printing chars only into columns
 * 0..78 leaves the cursor parked at column 79 WITHOUT wrapping, so no row ever
 * triggers the firmware's wrap-and-leave-a-cursor-block behaviour. */
static void blit_row_80(uint8_t r, const uint8_t *cells, uint8_t cols) {
  uint8_t c;
  a_vtab(r);
  for (c = 0; c < cols; ++c) a_cout(cells[c]);
}
#endif

/* Pin the quit hint to the extreme right of the message row in the rendered
 * grid, after editor_render has filled it (left-aligned status/help message) and
 * before the diff blit. Done here, not in editor_render, so the pure renderer
 * and its host tests stay unchanged; width-aware via ED_COLS_CUR (40 or 79). */
static void stamp_quit_hint(void) {
  static const char q[] = "^Q QUIT";
  uint8_t cols = (uint8_t)ED_COLS_CUR;
  uint8_t n = (uint8_t)(sizeof q - 1);
  uint8_t base, i;
  if (cols < n) return;
  base = (uint8_t)(cols - n);
  for (i = 0; i < n; ++i)
    s_screen.cells[ED_MSG_ROW][base + i] = (uint8_t)q[i];
}

#if ED_PRE_IIE
/* Stamp the active mode `[DGR]` / `[RAW]` onto the status row just after the
 * filename, so the //+ user can see at a glance whether the Swift typing model
 * is on. Done here (post-render, like stamp_quit_hint) so the pure renderer and
 * its status-row tests stay unchanged. The status row's right-justified L/C +
 * SAVED/EDITED label stays clear: a //+ buffer is only a few KB, so its line
 * numbers are small and the label hugs the right edge, well past this tag. */
static void stamp_mode_tag(const char *title) {
  const char *tag = s_cooked ? "[DGR]" : "[RAW]";
  uint8_t col = 1;                 /* render_status drew the filename from col 1 */
  uint8_t i;
  while (title[col - 1] && col < (uint8_t)ED_COLS) ++col;  /* col -> past name */
  ++col;                                                   /* one-space gap */
  for (i = 0; tag[i] && col < (uint8_t)ED_COLS; ++i)
    s_screen.cells[ED_STATUS_ROW][col++] = (uint8_t)tag[i];
}
#endif

/* Repaint only the rows that differ from the previously-blitted frame, then
 * record the new frame as the baseline. Width-aware: 40-col blits to video RAM,
 * 80-col through the firmware. */
static void blit_diff(void) {
  uint8_t r;
  uint8_t cols = ED_COLS_CUR;
#ifdef __CC65__
  if (ED_W80) ed_rebank_rom();          /* COUT needs motherboard ROM */
#endif
  for (r = 0; r < ED_ROWS; ++r) {
    if (memcmp(s_screen.cells[r], s_prev.cells[r], cols) != 0) {
#ifdef __CC65__
      if (ED_W80) blit_row_80(r, s_screen.cells[r], cols);
      else        blit_row(r, s_screen.cells[r]);
#else
      blit_row(r, s_screen.cells[r]);
#endif
      memcpy(s_prev.cells[r], s_screen.cells[r], cols);
    }
  }
}

#ifdef __CC65__
/* Blit a single grid row if it changed — the //e typing fast path repaints just
 * the edited row, and do_save repaints the status row for the SAVING indicator,
 * instead of rebuilding + diffing all 24. Width-aware like blit_diff. */
static void blit_one_row(uint8_t r) {
  uint8_t cols = ED_COLS_CUR;
  if (ED_W80) ed_rebank_rom();
  if (memcmp(s_screen.cells[r], s_prev.cells[r], cols) != 0) {
    if (ED_W80) blit_row_80(r, s_screen.cells[r], cols);
    else        blit_row(r, s_screen.cells[r]);
    memcpy(s_prev.cells[r], s_screen.cells[r], cols);
  }
}
#endif


/* Read the pending key directly from the Apple keyboard (caller has already
 * seen kbhit), bypassing cc65's cgetc so no second (conio) cursor is drawn. */
static uint8_t ed_readkey(void) {
  uint8_t k = (uint8_t)(*(volatile unsigned char *)0xC000 & 0x7F);
  *(volatile unsigned char *)0xC010 = 0; /* clear the keyboard strobe */
  return k;
}

/* Inverse-space screen code = a solid block (it's in the inverse range
 * $00-$3F, so it shows white-on-black regardless of INVFLG). */
#define ED_CURSOR_BLOCK 0x20

#ifdef __CC65__
/* Write one byte straight to the //e 80-column text page at (row, col), the
 * 80-col analogue of vid_row's direct 40-col write. The 80-col display
 * interleaves the two RAM banks: EVEN columns live in AUX, ODD columns in MAIN,
 * each a 40-byte line addressed by the same row formula. So the cell is
 * base + (col>>1), in AUX for an even col and MAIN for an odd one; 80STORE +
 * PAGE2 route the store to the right bank ($0400-$07FF only, so the
 * stack/zero-page are untouched). PAGE2 is left selecting MAIN, the state the
 * firmware COUT re-establishes per character.
 *
 * Why direct writes for the cursor here: drawing it through COUT needs a_vtab to
 * position, which zeroes the firmware column OURCH — and the //e firmware cursor
 * then leaves a stray inverse block at column 0 of the row that our redraw never
 * clears (the cursor row isn't dirty), piling up into a white bar down the left
 * edge. A direct poke never touches OURCH, so no firmware cursor is involved. */
static void vid_poke80(uint8_t row, uint8_t col, uint8_t b) {
  volatile uint8_t *cell = (volatile uint8_t *)(uint16_t)
    (0x0400u + ((uint16_t)(row & 7) << 7) + ((uint16_t)(row >> 3) * ED_COLS)
     + (col >> 1));
  __asm__("sta $C001");          /* 80STORE on: PAGE2 banks the text page    */
  if (col & 1) {
    __asm__("sta $C054");        /* PAGE2 -> MAIN (odd column)               */
    *cell = b;
  } else {
    __asm__("sta $C055");        /* PAGE2 -> AUX (even column)               */
    *cell = b;
    __asm__("sta $C054");        /* PAGE2 -> MAIN (restore)                  */
  }
}
#endif

#if defined(__CC65__) && defined(WITH_EDITOR)
/* Status-on-pause (both launcher disks; funded on II+ by dropping
 * the volume-picker disk-space readout). The fast paths skip the status repaint
 * to stay quick, so its line/col readout goes stale. read_key_blink calls this
 * once the user PAUSES (after an idle spell while it's blinking the cursor) — so
 * fast typing/moving pays nothing, the cursor stays visible the whole time, and
 * L/C catches up the moment you stop. The cursor position is recomputed from the
 * live gap buffer here (this runs only when idle, not per-keystroke), so no
 * per-frame snapshot is needed — just the stale flag. */
static const char *basename_of(const char *path);   /* defined below */
static uint8_t s_status_stale;
static void ed_status_refresh_if_stale(void) {
  uint16_t cpos, cline;
  const char *title;
  if (!s_status_stale) return;
  s_status_stale = 0;
  cpos = gapbuf_pos(&s_gb);
  cline = textnav_line_index(&s_gb, cpos);
  title = "UNTITLED";
  if (s_fname[0]) title = basename_of(s_fname);
  editor_status(cline, textnav_col(&s_gb, cpos), title, s_state.dirty,
                ED_COLS_CUR, s_screen.cells[ED_STATUS_ROW]);
#if ED_PRE_IIE
  stamp_mode_tag(title);   /* re-add the [DGR]/[RAW] tag render_status overwrote */
#endif
  blit_one_row(ED_STATUS_ROW);
}
#endif

#if defined(__CC65__) && defined(WITH_EDITOR)
/* Display column of `pos` within its logical line. Normally the byte offset,
 * but on the pre-IIe build in cooked mode `{`/`}`/`|` expand to 2/3 columns, so
 * the cursor-move fast path and the typing row-count must use DISPLAY columns,
 * not bytes — walk the line summing glyph widths. On the //e build (ED_PRE_IIE
 * == 0) it folds to the plain byte offset, byte-identical to a textnav_col call,
 * so that build's proven fast path is unchanged. */
#if ED_PRE_IIE
static uint16_t ed_disp_col(uint16_t pos, int expand) {
  uint16_t s = textnav_line_start(&s_gb, pos);
  if (expand) {
    uint16_t col = 0;
    for (; s < pos; ++s) col = (uint16_t)(col + editor_glyph_width(gapbuf_at(&s_gb, s), 1));
    return col;
  }
  return (uint16_t)(pos - s);
}
#else
#define ed_disp_col(pos, expand) textnav_col(&s_gb, (pos))
#endif
#endif

/* Wait for a key while flashing the cursor block at (row,col), writing video
 * RAM directly. The block is drawn immediately (so the cursor is visible the
 * instant it lands here, even on a blank cell) and the grid glyph is restored
 * before returning — both are single stores to the one cell, so no stray block
 * can ever be left behind. */
static uint8_t read_key_blink(uint8_t row, uint8_t col, uint8_t glyph) {
  uint8_t on = 1;
  uint16_t t = 0;
  uint8_t code;
  uint8_t *cell;
#ifdef __CC65__
  if (ED_W80) {
    /* 80-col: flash the cursor with direct writes to the interleaved text page
     * (vid_poke80) — same deterministic scheme as the 40-col path below, but
     * 80-col-addressed. The block is an inverse space ($20); the rest byte is
     * the glyph as COUT wrote it (canonical | $80, //e ALTCHAR lowercase). No
     * a_vtab/COUT, so the firmware cursor never leaves a stray column-0 block. */
    uint8_t rest = (uint8_t)(glyph | 0x80);
    vid_poke80(row, col, ED_CURSOR_BLOCK);   /* visible immediately */
    for (;;) {
      if (kbhit()) {
        vid_poke80(row, col, rest);          /* leave the cell as the grid has it */
        return ed_readkey();
      }
      if ((++t & BLINK_MASK) == 0) {
        on = (uint8_t)!on;
        vid_poke80(row, col, on ? ED_CURSOR_BLOCK : rest);
      }
#if defined(LITE_IIE)
      if (t == 0x2000u) ed_status_refresh_if_stale();  /* paused -> sync L/C */
#endif
    }
  }
#endif
  code = screen_code(glyph);
  cell = vid_row(row) + col;
  *cell = ED_CURSOR_BLOCK; /* visible immediately */
  for (;;) {
    if (kbhit()) {
      *cell = code; /* leave the cell as the grid has it */
      return ed_readkey();
    }
    /* Toggle the block once every BLINK_MASK+1 idle polls — the same cadence
     * as the launcher's file selector. */
    if ((++t & BLINK_MASK) == 0) {
      on = (uint8_t)!on;
      *cell = on ? ED_CURSOR_BLOCK : code;
    }
#if defined(__CC65__) && defined(WITH_EDITOR)
    if (t == 0x2000u) ed_status_refresh_if_stale();  /* paused -> sync L/C (both disks) */
#endif
  }
}

/* Width-aware single-char / cursor-position output for the prompts and the
 * message row. At 40-col the editor uses cc65 conio (direct to the 40-col
 * page); at 80-col the page isn't shown, so route through the firmware. */
static void ed_putc(uint8_t c) {
#ifdef __CC65__
  if (ED_W80) { a_cout(c); return; }
#endif
  cputc(c);
}
static void ed_gotoxy(uint8_t col, uint8_t row) {
#ifdef __CC65__
  if (ED_W80) { a_vtab(row); OURCH = col; return; }
#endif
  gotoxy(col, row);
}
/* Clear the message row (row 23) and home the cursor to its start. At 80-col
 * stop one short of the width so the final COUT doesn't wrap past the bottom
 * and scroll. */
static void ed_clear_msg_row(void) {
  uint8_t c;
  uint8_t cw = (uint8_t)ED_COLS_CUR;   /* 79 at 80-col: reserves the wrap column */
  ed_gotoxy(0, ED_MSG_ROW);
  for (c = 0; c < cw; ++c) ed_putc(' ');
  ed_gotoxy(0, ED_MSG_ROW);
}

/* Read a line on the message row (row 23) with the blinking cursor. Echoes
 * typed characters upper-cased (ProDOS filenames are upper-case). Return
 * accepts (returns 1, `out` NUL-terminated), Esc cancels (returns 0). The
 * caller invalidates s_prev afterward so the next frame repaints the row. */
static uint8_t editor_prompt(const char *label, char *out, uint8_t max) {
  uint8_t n;
  uint8_t base;
  uint8_t key;
  ed_clear_msg_row();
  for (base = 0; label[base]; ++base) ed_putc((unsigned char)label[base]);
  n = 0;
  for (;;) {
    key = read_key_blink(ED_MSG_ROW, (uint8_t)(base + n), ' ');
    if (key == 0x0D) { out[n] = '\0'; return 1; }     /* Return: accept */
    if (key == 0x1B) { out[0] = '\0'; return 0; }     /* Esc: cancel */
    if (key == 0x08 || key == 0x7F) {                 /* backspace */
      if (n) { --n; ed_gotoxy((uint8_t)(base + n), ED_MSG_ROW); ed_putc(' '); }
      continue;
    }
    if (key >= 0x20 && key < 0x7F && n < (uint8_t)(max - 1)) {
      if (key >= 'a' && key <= 'z') key = (uint8_t)(key - 0x20);
      out[n] = (char)key;
      ed_gotoxy((uint8_t)(base + n), ED_MSG_ROW);
      ed_putc(key);
      ++n;
    }
  }
}

/* The prompt and the file ops paint directly (bypassing the render grid),
 * so drop the dirty-row baseline to force a clean full repaint next frame. */
static void invalidate_screen(void) {
  memset(&s_prev, 0, sizeof s_prev);
}

/* The II+ cooked-mode key mapping (editor_cook_key) is a pure function in
 * keymap.c, host-tested there. */

/* Last path component, for the status line — s_fname holds the full path
 * (needed for save), but only the filename is worth showing. */
static const char *basename_of(const char *path) {
  const char *base = path;
  const char *p;
  for (p = path; *p; ++p) {
    if (*p == '/') base = p + 1;
  }
  return base;
}

#ifdef __CC65__
/* ProDOS MLI GET_PREFIX (editor_asm.s) -> gp_data = [len][prefix incl. '/']. */
extern void __fastcall__ editor_get_prefix(unsigned char *buf);
static unsigned char gp_data[66];
#endif

/* Make `name` absolute in `out` (capacity `cap`). Already-absolute names (a
 * leading '/') are copied verbatim, as is the fallback when the prefix can't be
 * read or the result wouldn't fit. The current ProDOS prefix is the boot volume
 * — the launcher restores it before chaining the editor and the editor only
 * ever opens by absolute path, so it never drifts — so a bare save-as / open
 * name resolves to a boot-volume file, and absolutising it lets
 * write_resume_note record a directory (new files preselect on return too) and
 * keeps re-saves stable. */
static void absolutize(const char *name, char *out, uint8_t cap) {
#ifdef __CC65__
  uint8_t plen;
  uint8_t nlen;
  uint8_t i;
  uint8_t k;
  if (name[0] == '/') { strcpy(out, name); return; }
  gp_data[0] = 0;                  /* MLI leaves len 0 if there's no prefix */
  editor_get_prefix(gp_data);
  plen = gp_data[0];
  nlen = (uint8_t)strlen(name);
  if (plen == 0 || plen > 64 || (uint16_t)(plen + nlen + 1) > cap) {
    strcpy(out, name);
    return;
  }
  k = 0;
  for (i = 0; i < plen; ++i) out[k++] = (char)gp_data[1 + i]; /* /VOL/.../ */
  for (i = 0; i < nlen; ++i) out[k++] = name[i];
  out[k] = '\0';
#else
  (void)cap;
  strcpy(out, name);
#endif
}

/* Save the buffer, prompting SAVE AS: when the file is untitled. Returns 1
 * if it saved, 0 if it didn't (name prompt cancelled, or write error). Sets
 * *pmsg to the result. Shared by Ctrl-S and the save-on-quit/open path. */
static int do_save(const char **pmsg) {
  static char target_buf[64];
  char name[64];
  const char *target = s_fname;
  if (!s_fname[0]) {
    if (!editor_prompt("SAVE AS: ", name, (uint8_t)sizeof name) || !name[0]) {
      *pmsg = idle_help();
      return 0;
    }
    /* Resolve a bare name against the boot volume so s_fname ends up absolute
     * (and the file thus has a directory to record for the resume note). */
    absolutize(name, target_buf, (uint8_t)sizeof target_buf);
    target = target_buf;
  }
  {
    int ok;
    /* Progress goes in the STATUS row (top right), NOT the message row, so the
     * bottom legend stays put: overwrite the status row's state field
     * ("EDITED"/"SAVED") with "SAVING" during the (blocking) write. The caller's
     * repaint then shows "SAVED" there (dirty cleared) while the bottom keeps
     * the legend. */
#ifdef __CC65__
    {
      static const char sv[] = "SAVING";
      uint8_t *sr = s_screen.cells[ED_STATUS_ROW];
      uint8_t base = (uint8_t)((uint8_t)ED_COLS_CUR - 6);
      uint8_t i;
      for (i = 0; i < 6; ++i) sr[base + i] = (uint8_t)sv[i];
      blit_one_row(ED_STATUS_ROW);
    }
#endif
    ok = (editor_file_save(&s_gb, target, s_cooked) == EFIO_OK);
    ed_rebank_rom();   /* cc65 file I/O banked LC-RAM; restore ROM for COUT */
    if (ok) {
      if (target != s_fname) strcpy(s_fname, target);
      s_state.dirty = 0;
      *pmsg = idle_help();   /* keep the bottom legend; SAVED shows top right */
      return 1;
    }
  }
  *pmsg = "WRITE ERROR";
  return 0;
}

/* Ask whether to save unsaved changes before quitting / opening another
 * file. Returns 1 = save, 0 = discard, -1 = cancel (keep editing). */
static int confirm_save(void) {
  const char *p;
  uint8_t k;
  ed_clear_msg_row();
  for (p = "SAVE CHANGES? Y=YES N=NO ESC=CANCEL"; *p; ++p) {
    ed_putc((unsigned char)*p);
  }
  for (;;) {
    while (!kbhit()) { }
    k = ed_readkey();
    if (k >= 'a' && k <= 'z') k = (uint8_t)(k - 0x20);
    if (k == 'Y') return 1;
    if (k == 'N') return 0;
    if (k == 0x1B || k == 'C') return -1;
  }
}

/* The absolute path the editor last saved to, for the launcher's Ctrl-R run.
 * s_fname is editor BSS (not the GAPBUF window), so it survives the return. */
const char *editor_saved_path(void) { return s_fname; }

/* The cursor's buffer offset at the last Ctrl-R, so the launcher can record it
 * in the LASTRUN note and feed it back on resume. Editor BSS, survives like
 * s_fname. */
uint16_t editor_saved_cursor(void) { return s_run_cursor; }

int editor_main(const char *path, uint16_t start_cursor) {
  const char *msg;
  unsigned char key;
  EditorAction act;
  uint8_t cr;
  uint8_t cc;
  /* Cached line count for the render's gutter. Rescanning the whole buffer
   * every keystroke (just to size the gutter) was the single biggest slice of
   * a keystroke's cost (profiling). It only changes on a newline insert/delete,
   * so recompute solely when `recount` is set — after a load, or after a key
   * that could add/remove a '\n'. Plain cursor moves and ordinary typing leave
   * it untouched and pay no scan. */
  uint16_t line_count;
  uint8_t recount = 1;            /* force the initial scan on the first frame */
#if defined(__CC65__) && defined(WITH_EDITOR)
  /* Cursor-move fast path: when the previous key only moved the cursor (no text
   * edit, no scroll), the grid is unchanged, so skip the full rebuild + 24-row
   * diff and just relocate the cursor + refresh the status row. `locate` is set
   * after each dispatch for the NEXT frame; top_before captures the scroll
   * position to detect whether the view moved. Built into both launcher disks
   * (WITH_EDITOR); the status-on-pause L/C refresh is //e-only (no II+ room). */
  uint8_t locate = 0;
  uint8_t typefast = 0;   /* like locate, but for a 1-row printable edit */
  uint16_t top_before;
  const char *prev_msg = 0;  /* message shown on the last full render */
#endif

  /* Take the launcher-supplied file path (absolute; NULL/empty = new scratch
   * file) before touching the gap buffer — gapbuf_init writes s_text, which now
   * lives in the same low-RAM window the caller's buffers may sit in. */
  s_fname[0] = '\0';
  if (path && path[0]) {
    uint8_t i;
    for (i = 0; i + 1 < (uint8_t)sizeof s_fname && path[i]; ++i)
      s_fname[i] = path[i];
    s_fname[i] = '\0';
  }

  gapbuf_init(&s_gb, s_text, (uint16_t)GAPBUF_CAP);
  editor_state_init(&s_state, &s_gb);
  /* The launcher already set the machine up at boot; the editor just takes over
   * the screen. Suppress cc65's conio cursor (we draw our own blinking block),
   * clear the browser's display, and zero the dirty-diff baseline so the first
   * blit_diff repaints fully. s_prev is NOT freshly-zeroed BSS here (no reboot
   * now — the editor is re-entered across browse sessions), so the memset is
   * load-bearing, not belt-and-braces. */
  cursor(0);
#ifdef __CC65__
  if (ED_W80) a_home(); else clrscr();   /* 80-col: firmware clear, not the 40-col page */
#else
  clrscr();
#endif
  memset(&s_prev, 0, sizeof s_prev);
  /* Default mode from the extension: .swift (and untitled scratch) start in
   * cooked/digraph mode, anything else in raw mode. The //+ user retoggles with
   * Ctrl-G. Set before the load so editor_file_load uses the matching split. */
  s_cooked = (uint8_t)editor_path_is_swift(s_fname);
  msg = idle_help();
  if (s_fname[0]) {
    EditorFileResult lr = editor_file_load(&s_gb, s_fname, s_cooked);
    if (lr != EFIO_OK) {
      s_fname[0] = '\0'; /* couldn't open -> empty scratch buffer */
      gapbuf_init(&s_gb, s_text, (uint16_t)GAPBUF_CAP);
      /* Distinguish a file too big for the gap buffer (xbig.swift and other
       * Family B programs exceed GAPBUF_CAP) from a missing/unreadable file,
       * so "too big to edit here" doesn't masquerade as a broken file. */
      msg = (lr == EFIO_TOOBIG) ? "FILE TOO BIG TO EDIT"
                                : "COULD NOT OPEN FILE";
    } else if (start_cursor) {
      /* Restore the cursor where Ctrl-R left it (edit -> run -> edit). The
       * save->load round-trip reproduces the same buffer, so the offset still
       * lands; gapbuf_move_to clamps it. Pre-scroll so the line is on screen
       * (the WRAP retry loop below fine-tunes the top). */
      gapbuf_move_to(&s_gb, start_cursor);
      s_state.top_line = editor_scroll(&s_gb, 0, start_cursor);
    }
  }

  for (;;) {
    const char *title = "UNTITLED";
    uint16_t cpos = gapbuf_pos(&s_gb);
    uint16_t cline = textnav_line_index(&s_gb, cpos);
    /* Cooked mode drives both the pre-IIe digraph expansion (passed to
     * editor_render) and the inverse-case swap (screen_code). s_cooked is set at
     * load (extension default) and flipped by Ctrl-G, NOT re-derived here, so a
     * toggle sticks. ED_PRE_IIE is 0 on the //e disk (real braces + native case,
     * no swap regardless). */
    int expand_digraphs = ED_PRE_IIE && s_cooked;
#if defined(__CC65__) && defined(WITH_EDITOR)
    uint8_t fast = 0;
#endif
    if (s_fname[0]) title = basename_of(s_fname);
    if (recount) { line_count = textnav_line_count(&s_gb); recount = 0; }
#if defined(__CC65__) && defined(WITH_EDITOR)
    /* The fast paths don't repaint the message row, so when the message changes
     * (e.g. SAVED -> the legend on the next key, or a LOADED / error) force a
     * full render this frame so row 23 actually updates. */
    if (msg != prev_msg) { locate = 0; typefast = 0; }
    /* Cursor-move fast path: the last full render recorded each visible line's
     * start row in s_screen.line_row, so the cursor's screen position is a table
     * lookup (its line's start row + which wrapped sub-row the column falls on)
     * — no buffer walk, no grid rebuild, no diff. The text grid is unchanged, so
     * we don't even repaint: read_key_blink just needs the new (row,col). The
     * line/col readout in the status bar therefore lags during pure movement and
     * catches up on the next full render (any edit or scroll). Anything off the
     * recorded screen, or where the cursor's wrapped row falls past the bottom,
     * falls through to the full render below. */
    if (locate && cline >= s_state.top_line) {
      uint8_t idx = (uint8_t)(cline - s_state.top_line);
      if (idx < ED_WORK_ROWS && g_ed_line_row[idx] != 0xFF
#if ED_PRE_IIE
          /* //+ digraphs (`{`/`}`/`|` = 2/3 cols) can leave a wrapped row holding
           * fewer than text_w display columns (editor_render_wrapped never splits
           * a glyph), so the ccol/text_w row arithmetic is exact only for a line
           * that occupies ONE screen row. Prove that from the table — the next
           * visible line starts exactly one row below — else fall through to a
           * full render. (//e has no digraphs, so it keeps the wrap-aware path.) */
          && (uint8_t)(idx + 1) < ED_WORK_ROWS
          && g_ed_line_row[idx + 1] == (uint8_t)(g_ed_line_row[idx] + 1)
#endif
         ) {
        uint8_t gutter = editor_gutter_width(line_count);
        uint8_t text_w = (uint8_t)(ED_COLS_CUR - gutter);
        uint16_t ccol = ed_disp_col(cpos, expand_digraphs);
        uint8_t row = (uint8_t)(g_ed_line_row[idx] + (uint8_t)(ccol / text_w));
        if (row < ED_WORK_ROWS) {
          s_screen.cur_row = (uint8_t)(ED_WORK_TOP + row);
          s_screen.cur_col = (uint8_t)(gutter + (uint8_t)(ccol % text_w));
          fast = 1;
        }
      }
    }
    locate = 0;              /* one-shot; recomputed after the next dispatch */
    /* Typing fast path: a printable insert that left its line's row COUNT
     * unchanged changed only that line's row(s) — re-render just them (1 row, or
     * several when the line wraps) and blit them, instead of the whole 24-row
     * grid + diff. The line-start table stays valid (no line added, nothing
     * below shifted), so the line's first row is a table lookup;
     * editor_render_wrapped repaints its rows and places the cursor. Both the
     * render and the arming (see the dispatch tail) are now wrap-aware on both
     * disks — a multi-row line whose row COUNT is unchanged repaints all its
     * rows. */
#if defined(__CC65__) && defined(WITH_EDITOR)
    if (!fast && typefast && cline >= s_state.top_line) {
      uint8_t idx = (uint8_t)(cline - s_state.top_line);
      if (idx < ED_WORK_ROWS && g_ed_line_row[idx] != 0xFF) {
        uint8_t gutter = editor_gutter_width(line_count);
        uint8_t row0 = g_ed_line_row[idx];
        uint16_t end;
        uint8_t rows;
        s_screen.cur_set = 0;        /* let editor_render_wrapped place the cursor */
        rows = editor_render_wrapped(&s_gb, textnav_line_start(&s_gb, cpos),
                                     (uint16_t)(cline + 1), row0, cpos,
                                     expand_digraphs, ED_COLS_CUR, gutter,
                                     &s_screen, &end);
        if (s_screen.cur_set) {      /* cursor landed on a rendered row */
          uint8_t rr;
          for (rr = 0; rr < rows; ++rr)
            blit_one_row((uint8_t)(ED_WORK_TOP + row0 + rr));
          fast = 1;
        }
      }
    }
#endif
    typefast = 0;
    if (!fast)
#endif
    {
    /* keymap's follow_cursor scrolls one display row per logical line
     * (editor_scroll), which under-counts in WRAP mode where a long line spans
     * several rows — the cursor's line could climb off the bottom without the
     * view ever moving. Render, and if the cursor didn't land on screen
     * (cur_set == 0) drop the top one logical line and re-render, until it
     * shows or the top reaches the cursor's own line (a single line longer than
     * the work area then shows from its start). Only the final frame is
     * blitted, so the retries never flicker. */
    for (;;) {
      editor_render(&s_gb, s_state.top_line, cpos, cline, line_count,
                    title, s_state.dirty, msg, expand_digraphs, ED_COLS_CUR,
                    s_linemode, &s_screen);
      if (s_screen.cur_set || s_state.top_line >= cline) break;
      ++s_state.top_line;
    }
    stamp_quit_hint();   /* pin ^Q QUIT to the far right of the message row */
#if ED_PRE_IIE
    stamp_mode_tag(title);   /* show [DGR]/[RAW] after the filename (status row) */
#endif
    blit_diff();
    }
#if defined(__CC65__) && defined(WITH_EDITOR)
    prev_msg = msg;   /* what's now shown on the message row (full render only) */
#endif
#if defined(__CC65__) && defined(WITH_EDITOR)
    /* On a fast frame the status row wasn't repainted, so mark it stale: when
     * the user pauses, read_key_blink refreshes it (while still blinking the
     * cursor). A full frame painted it, so it's current. (Both launcher disks.) */
    s_status_stale = fast;
#endif
    cr = s_screen.cur_row;
    cc = s_screen.cur_col;
    key = read_key_blink(cr, cc, s_screen.cells[cr][cc]);
#if ED_PRE_IIE
    /* On a II+ in Swift/cooked mode, Ctrl-W is the input-method key for `_`
     * (the II+ keyboard has no `_` key) and typed A-Z auto-lowercase to the
     * canonical buffer form. editor_cook_key applies both; raw mode keeps the
     * bytes as-typed. Letters/$17 are never editor commands, so this only
     * touches text (the arrows and Ctrl-O/Ctrl-L $0C cursor moves pass through
     * unchanged; the mode toggle below is Ctrl-G $07). */
    if (s_cooked) key = editor_cook_key((uint8_t)key);
    /* Ctrl-G toggles cooked (digraph) <-> raw mode. (It moved off Ctrl-L, which
     * is now move-down per Apple Pascal; Ctrl-W is the `_` input key above.) The
     * buffer's byte representation differs between modes (cooked holds input
     * form: lowercase + ' case markers + literal `<%`; raw holds the bytes
     * as-is), so a bare flip mid-edit would misread the existing text. Reload
     * the file in the new mode instead (option 2): confirm any unsaved edits
     * first, flip s_cooked, then re-read from disk through the matching load
     * split so display/entry/save stay consistent. An untitled scratch buffer
     * has nothing to reload, so it just switches mode (bytes carry over). */
    if (key == 0x07) {
      if (s_state.dirty) {
        int r = confirm_save();
        if (r < 0) { msg = idle_help(); invalidate_screen(); continue; }
        if (r == 1 && !do_save(&msg)) { invalidate_screen(); continue; }
      }
      s_cooked = (uint8_t)!s_cooked;
      if (s_fname[0]) {
        EditorFileResult lr = editor_file_load(&s_gb, s_fname, s_cooked);
        ed_rebank_rom();   /* cc65 file I/O banked LC-RAM; restore ROM for COUT */
        if (lr == EFIO_OK) {
          s_state.dirty = 0;
          s_state.top_line = 0;
          recount = 1;         /* buffer reloaded in the new mode */
          msg = idle_help();   /* the status-row [DGR]/[RAW] tag flips to confirm */
        } else {
          msg = "RELOAD ERROR";
        }
      } else {
        msg = idle_help();
      }
      invalidate_screen();
      continue;
    }
#endif
#if defined(__CC65__) && defined(LITE_IIE)
    if (key == 0x17) {            /* Ctrl-W: fall back / switch text width 40<->80 (//e) */
      ed_width((unsigned char)!g_width80);
      if (ED_W80) a_home(); else clrscr();
      memset(&s_prev, 0, sizeof s_prev);   /* repaint fully at the new width */
      locate = 0; typefast = 0;            /* screen cleared -> full render next */
      continue;
    }
#endif
    /* Only a newline insert (Return) or a delete can change the line count, so
     * rescan it next frame just for those — ordinary typing and cursor moves
     * (the arrows included, now that ← is a non-destructive move) leave the
     * cached count valid and pay no whole-buffer scan. The deletes are Ctrl-D
     * and the //e Delete key ($7F). */
    if (key == ED_KEY_RETURN || key == ED_KEY_DELETE ||
        key == ED_KEY_CTRL_D) {
      recount = 1;
    }
#if defined(__CC65__) && defined(WITH_EDITOR)
    top_before = s_state.top_line;
#endif
    act = editor_dispatch(&s_state, (uint8_t)key);
#if defined(__CC65__) && defined(WITH_EDITOR)
    /* Both fast paths require the key to have only moved/typed within the view:
     * no I/O action and no scroll (`still`). A printable insert (`printable`)
     * arms the typing path; any cursor-move that isn't an edit arms `locate`. */
    {
      uint8_t printable = (key >= 0x20 && key < 0x7F);
      uint8_t still = (act == ED_ACT_NONE) && (s_state.top_line == top_before);
    /* locate: only the cursor moved — not an edit (Return / delete / printable
     * insert) key — so the grid is unchanged and we just relocate the cursor.
     * The bare arrows (← $08 included) are moves now, so they take this path. */
    locate = still && !(key == ED_KEY_RETURN || key == ED_KEY_DELETE ||
                        key == ED_KEY_CTRL_D || printable);
    /* ...or the typing fast path iff this was a printable insert (no scroll)
     * that left its line's screen-ROW COUNT unchanged — then only that line's
     * row(s) changed and the lines below didn't shift, so we repaint just that
     * line (one row, or several when it wraps in 40-col). An insert that spilled
     * the line onto a new row changes the count -> full render. */
    typefast = 0;
    if (still && printable) {
      uint16_t p = gapbuf_pos(&s_gb);
      /* DISPLAY width of the line after the insert (digraph-aware on //+), so the
       * test below matches how editor_render_wrapped will actually lay it out. */
      uint16_t llen = ed_disp_col(textnav_line_end(&s_gb, p), expand_digraphs);
      uint8_t tw = (uint8_t)((uint8_t)ED_COLS_CUR - editor_gutter_width(line_count));
      /* Arm whenever the insert left the line's screen-ROW COUNT unchanged (a
       * wrapped line that didn't spill onto a new row) — then only that line's
       * rows changed and nothing below shifted, so editor_render_wrapped can
       * repaint just those rows. An insert that wrapped onto a new row changes
       * the count -> full render. Wrap-aware on BOTH disks since the II+
       * disk-space-readout drop freed the budget (the II+ build was single-row
       * only before). ed_disp_col gives digraph-aware display width, so the row
       * count matches how editor_render_wrapped actually lays the line out. */
      uint16_t ra = llen ? (uint16_t)((llen + tw - 1) / tw) : 1;       /* rows now */
      uint16_t rb = (llen > 1) ? (uint16_t)((llen - 1 + tw - 1) / tw) : 1; /* before */
      if (ra == rb) typefast = 1;
    }
    }
#endif
    switch (act) {
      case ED_ACT_QUIT: {
        /* Guard unsaved changes: Save / Don't Save / Cancel. */
        if (s_state.dirty) {
          int r = confirm_save();
          if (r < 0) { msg = idle_help(); invalidate_screen(); break; }
          if (r == 1 && !do_save(&msg)) { invalidate_screen(); break; }
        }
        /* Return to the launcher's file browser in-process — no cold reboot,
         * no LASTRUN breadcrumb (the merged launcher just redraws its panes
         * and we both kept ProDOS MLI resident). */
#ifdef __CC65__
        /* Raw ProDOS file I/O (pf_open/pf_read/pf_write, used for load +
         * save) leaves the language card banked to read RAM. The launcher's
         * screen output is ROM-based (a_cout -> jmp $FDED) and its LC is empty,
         * so re-bank
         * motherboard ROM before handing back — otherwise the launcher's next
         * COUT runs off uninitialised LC RAM and crashes into the monitor. */
        __asm__("bit $C082");
#endif
        return EDITOR_QUIT;
      }
      case ED_ACT_SAVE:
        do_save(&msg);
        invalidate_screen();
        break;
      case ED_ACT_RUN:
        /* Save, then hand back to the launcher to stage + chain an interpreter
         * on the saved file. do_save prompts SAVE AS when untitled, writes the
         * canonical source, sets s_fname absolute, and re-banks ROM; a
         * cancelled or failed save (or a still-untitled buffer) keeps us in the
         * editor. The launcher reads editor_saved_path() on the EDITOR_RUN
         * return. */
        if (!do_save(&msg) || !s_fname[0]) { invalidate_screen(); break; }
        s_run_cursor = gapbuf_pos(&s_gb);   /* resume here after the run */
        return EDITOR_RUN;
      default:
        msg = idle_help();   /* quit hint is pinned at the right already */
        break;
    }
  }
}
