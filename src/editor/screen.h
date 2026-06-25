/* Editor screen model — a pure 24x40 frame built from the gap buffer.
 *
 * The renderer turns the buffer + cursor + viewport into a grid of display
 * glyphs plus the cursor's screen position. It is platform-free: the
 * target blit (a later slice) copies the grid to video RAM, and the host
 * tests assert on the grid directly. Keeping the wrap/scroll/cursor maths
 * here — where it can be unit-tested — is the whole point of the split.
 *
 * Layout (design doc 006):
 *   row 0      status line  (filename, dirty `*`, current line number)
 *   rows 1-22  work area     (22 logical lines, one per row)
 *   row 23     message line  (prompts / confirmations / errors)
 *
 * Long lines: one logical line draws on exactly one work row, truncated
 * to 40 display columns with a '>' overflow marker in the last column; the
 * cursor may still sit logically past the visible tail (its screen column
 * clamps to 39). Only vertical scrolling is modelled.
 *
 * Display width: a byte is one display column on a //e (native lowercase),
 * but on a pre-IIe screen `{`/`}` draw as `<%`/`%>` (2 cols) and `|` as
 * `??!` (3 cols) per design doc 003. The renderer expands those and budgets
 * each row by *display* width when `pre_iie` is set, so brace-heavy //+
 * lines align. The width-preserving case->video mapping is NOT done
 * here — the blit applies it per cell so the grid stays plain ASCII (and
 * readable in tests).
 *
 * Pure C90; compiles on host and target.
 */
#ifndef SWIFTII_EDITOR_SCREEN_H
#define SWIFTII_EDITOR_SCREEN_H

#include <stdint.h>

#include "gapbuf.h"

#define ED_COLS       40    /* default / 40-col width (host tests, 40-col mode) */
/* Frame allocation: the widest a row can be. 80 only where the editor can
 * actually enter 80-col mode — the //e launcher (LITE_IIE) and the host
 * (tests exercise both widths). The II+ launcher build never leaves 40
 * columns (Ctrl-W is LITE_IIE-gated), and the 40-col EditorScreen
 * is ~960 B smaller — RAM the GAPBUF region hands back to the gap buffer,
 * raising the II+ editor's file capacity (doc 016). */
#if defined(__CC65__) && !defined(LITE_IIE)
#define ED_COLS_MAX   40
#else
#define ED_COLS_MAX   80
#endif
#define ED_ROWS       24
#define ED_STATUS_ROW 0
#define ED_WORK_TOP   1
#define ED_WORK_ROWS  22
#define ED_MSG_ROW    23
/* Left line-number gutter on each work row: a right-justified line number then
 * a space. Its width is dynamic — editor_gutter_width() sizes it to the file's
 * largest line number's digit count plus one, so small files don't waste a wide
 * left margin. The text area is the remaining cols - gutter columns. */

/* Long-line display mode for the work area:
 *   TRUNCATE - one logical line per row, clipped at the width with a '>'
 *   HSCROLL  - one logical line per row, shifted horizontally to keep the
 *              cursor visible ('<' / '>' mark hidden text)
 *   WRAP     - a long logical line flows onto the next row(s) */
#define ED_MODE_TRUNCATE 0
#define ED_MODE_HSCROLL  1
#define ED_MODE_WRAP     2

typedef struct editor_screen {
  uint8_t cells[ED_ROWS][ED_COLS_MAX]; /* display glyphs, space-filled */
  uint8_t cur_row;
  uint8_t cur_col;
  uint8_t cur_set;   /* 1 if the cursor's position is on screen at this top_line,
                        0 if its line is scrolled off (cur_row/col then park at
                        the top). The editor uses this to scroll WRAP mode: a
                        long line spans several rows, so it re-renders one line
                        lower until the cursor lands, instead of assuming one
                        row per logical line. */
} EditorScreen;

/// Render one logical line (starting at `start`) across its wrapped rows into
/// out->cells from work row `row0`, the per-line core of the WRAP layout shared
/// by editor_render and the editor's typing fast path. Tracks the cursor (sets
/// out->cur_row/col/set if `cursor_pos` lands in the line and cur_set is 0),
/// writes the line-end position to *out_end, and returns the row count filled.
uint8_t editor_render_wrapped(const GapBuf *gb, uint16_t start, uint16_t line_no,
                              uint8_t row0, uint16_t cursor_pos, int pre_iie,
                              uint8_t cols, uint8_t gutter, EditorScreen *out,
                              uint16_t *out_end);

/* The line-start table backs the cursor-move / typing fast paths — built into
 * every in-launcher editor build (WITH_EDITOR, both disks) plus the host. */
#if defined(LITE_IIE) || defined(WITH_EDITOR) || !defined(__CC65__)
#define ED_HAVE_LINE_ROW 1
#endif
/* editor_status (deferred status-row refresh) backs the status-on-pause polish
 * on BOTH launcher builds (ported to the II+ disk too, funded by
 * dropping the volume-picker disk-space readout — its L/C readout used to lag
 * until the next full render). */
#if defined(LITE_IIE) || defined(WITH_EDITOR) || !defined(__CC65__)
#define ED_HAVE_STATUS 1
#endif

#ifdef ED_HAVE_LINE_ROW
/* The work-area screen row each visible logical line STARTS on, indexed by
 * (line_index - top_line); 0xFF where no line starts (blank tail). A full
 * editor_render (WRAP path) fills it so the cursor fast paths can place the
 * cursor / locate the edited line's rows by table lookup — correct even when
 * lines wrap (40-col) — instead of walking the buffer. One copy (a global, not
 * an EditorScreen field, so it isn't duplicated into the diff baseline s_prev);
 * the fast path always reads the last full render's table. */
extern uint8_t g_ed_line_row[ED_WORK_ROWS];
#endif

#ifdef ED_HAVE_STATUS
/// Render only the status row into `row` — the fast path's deferred status
/// refresh (line/col readout) when the user pauses. //e / host only.
void editor_status(uint16_t cursor_line, uint16_t cursor_col,
                   const char *filename, int dirty, uint8_t cols, uint8_t *row);
#endif

/// Display width (1..3) of one logical byte. `pre_iie` non-zero selects the
/// //+ widths (`{`/`}` = 2, `|` = 3); otherwise everything is 1.
uint8_t editor_glyph_width(uint8_t ch, int pre_iie);

/// Line-number gutter width for a buffer with `line_count` logical lines:
/// the largest line number's digit count plus one trailing space (so a small
/// file uses a narrow gutter). editor_render computes this internally from the
/// buffer; exposed so callers/tests can reproduce the layout.
uint8_t editor_gutter_width(uint16_t line_count);

/// Choose the top work-area line so the cursor's line is on screen, given
/// the current `top_line`. Returns the (possibly unchanged) new top line.
/// One display row per logical line — exact for TRUNCATE/HSCROLL; in WRAP mode
/// a long line spans several rows, so the caller re-renders one line lower
/// while EditorScreen.cur_set stays 0 (see editor.c) to finish the scroll.
uint16_t editor_scroll(const GapBuf *gb, uint16_t top_line,
                       uint16_t cursor_pos);

/// Render the full frame into `out`, `cols` columns wide (ED_COLS for 40-col,
/// ED_COLS_MAX for 80-col). `top_line` is the logical line shown on the first
/// work row (call editor_scroll first to keep the cursor visible). `cursor_line`
/// is the cursor's logical line index and `line_count` the buffer's total line
/// count — the caller passes cached values so this hot path needn't rescan the
/// whole buffer every keystroke (profiling: those scans were ~90% of the cost);
/// `textnav_line_index(gb, cursor_pos)` and `textnav_line_count(gb)` reproduce
/// them. `filename` / `message` may be NULL (treated as empty). `pre_iie`
/// selects the display-width / digraph-expansion behaviour.
void editor_render(const GapBuf *gb, uint16_t top_line, uint16_t cursor_pos,
                   uint16_t cursor_line, uint16_t line_count,
                   const char *filename, int dirty, const char *message,
                   int pre_iie, uint8_t cols, uint8_t mode, EditorScreen *out);

#endif /* SWIFTII_EDITOR_SCREEN_H */
