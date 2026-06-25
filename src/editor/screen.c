/* Editor screen renderer — see screen.h for the model and layout.
 *
 * The only design-003 substitution reproduced here is the *width-changing*
 * digraph expansion (`{`/`}`/`|`), because budgeting a 40-column row needs
 * to know how wide each glyph draws. The width-preserving case->video
 * mapping is left to the blit, so the grid this builds stays plain ASCII. */
#include "screen.h"

#include "textnav.h"

#ifdef ED_HAVE_LINE_ROW
uint8_t g_ed_line_row[ED_WORK_ROWS];   /* see screen.h — cursor-move fast path */
#endif

/* Expand one logical byte into its display glyphs. Writes up to 3 bytes to
 * `out` and returns the count (the display width). On a //e (`pre_iie` 0)
 * every byte is itself. */
static uint8_t expand_glyph(uint8_t ch, int pre_iie, uint8_t *out) {
  if (pre_iie) {
    if (ch == '{') { out[0] = '<'; out[1] = '%'; return 2; }
    if (ch == '}') { out[0] = '%'; out[1] = '>'; return 2; }
    if (ch == '|') { out[0] = '?'; out[1] = '?'; out[2] = '!'; return 3; }
  }
  out[0] = ch;
  return 1;
}

uint8_t editor_glyph_width(uint8_t ch, int pre_iie) {
  uint8_t glyphs[3];
  return expand_glyph(ch, pre_iie, glyphs);
}

uint16_t editor_scroll(const GapBuf *gb, uint16_t top_line,
                       uint16_t cursor_pos) {
  uint16_t line = textnav_line_index(gb, cursor_pos);
  if (line < top_line) return line;
  if (line >= (uint16_t)(top_line + ED_WORK_ROWS)) {
    return (uint16_t)(line - ED_WORK_ROWS + 1);
  }
  return top_line;
}

static void fill_row(uint8_t *row, uint8_t ch, uint8_t cols) {
  uint8_t c;
  for (c = 0; c < cols; ++c) row[c] = ch;
}

/* Copy a NUL-terminated string into a row from `col`, truncating at the
 * row edge (`cols`). Returns the column just past the last byte written. */
static uint8_t put_str(uint8_t *row, uint8_t col, const char *s, uint8_t cols) {
  if (s == 0) return col;
  while (*s && col < cols) row[col++] = (uint8_t)*s++;
  return col;
}

/* Decimal digit count of `n` (1 for 0). */
static uint8_t u16_digits(uint16_t n) {
  uint8_t d = 1;
  while (n >= 10) { n = (uint16_t)(n / 10); ++d; }
  return d;
}

/* Line-number gutter width: enough columns for the largest line number's
 * digits, plus one trailing space. So a <=9-line file uses a 2-column gutter,
 * a 10..99-line file 3, etc. — no fixed left margin wasted on small files. */
uint8_t editor_gutter_width(uint16_t line_count) {
  if (line_count == 0) line_count = 1;
  return (uint8_t)(u16_digits(line_count) + 1);
}

/* Decimal-format `n` into `buf` (max 6 bytes incl. NUL for a uint16). */
static void u16_to_dec(uint16_t n, char *buf) {
  char tmp[5];
  uint8_t i;
  uint8_t j;
  i = 0;
  if (n == 0) tmp[i++] = '0';
  while (n > 0) { tmp[i++] = (char)('0' + (n % 10)); n = (uint16_t)(n / 10); }
  j = 0;
  while (i > 0) buf[j++] = tmp[--i];
  buf[j] = '\0';
}

static void render_status(uint16_t cursor_line, uint16_t cursor_col,
                          const char *filename, int dirty, uint8_t cols,
                          uint8_t *row) {
  char num[6];
  char label[28];
  const char *state;
  uint8_t k;
  uint8_t i;
  fill_row(row, ' ', cols);
  /* Leading '*' (dirty) plus an explicit SAVED/EDITED word on the right so
   * the save state is unmistakable. */
  row[0] = (uint8_t)(dirty ? '*' : ' ');
  put_str(row, 1, filename, cols);
  /* Right side: the cursor's line (L) and column (C), both 1-based, then the
   * save state. */
  k = 0;
  label[k++] = 'L';
  u16_to_dec((uint16_t)(cursor_line + 1), num);
  for (i = 0; num[i]; ++i) label[k++] = num[i];
  label[k++] = ' ';
  label[k++] = 'C';
  u16_to_dec((uint16_t)(cursor_col + 1), num);
  for (i = 0; num[i]; ++i) label[k++] = num[i];
  label[k++] = ' ';
  label[k++] = ' ';
  state = dirty ? "EDITED" : "SAVED";
  for (i = 0; state[i]; ++i) label[k++] = (char)state[i];
  label[k] = '\0';
  /* Right-justified; wins over the filename on overlap. */
  put_str(row, (uint8_t)(cols - k), label, cols);
}

/* Render the logical line starting at `start` (up to the next '\n' or
 * `len`) into a work row, display-width budgeted, with a '>' overflow
 * marker if it doesn't all fit. Returns the line-end position (the '\n'
 * index, or `len`).
 *
 * Performance: the buffer is read inline (gb->buf with the gap split) and
 * the width-1 case is the fast path, so a row costs no per-byte function
 * calls — this is the hot loop on every keystroke and gapbuf_at /
 * gapbuf_len / expand_glyph calls per byte were the typing lag. expand_glyph
 * is still used for the rare brace/pipe bytes so the glyph table lives in
 * one place. */
/* Write the line-number gutter (right-justified `line_no`, then a space) into
 * cols 0..gutter-1 of `row`. `gutter` is sized to the file's largest line
 * number (editor_gutter_width). */
static void render_gutter(uint16_t line_no, uint8_t gutter, uint8_t *row) {
  char num[6];
  uint8_t n;
  uint8_t startcol;
  uint8_t j;
  u16_to_dec(line_no, num);
  for (n = 0; num[n]; ++n) { /* count digits */ }
  startcol = n < gutter ? (uint8_t)(gutter - 1 - n) : 0;
  for (j = 0; j < n && (uint8_t)(startcol + j) < gutter; ++j) {
    row[startcol + j] = (uint8_t)num[j];
  }
}

static uint16_t render_work_line(const GapBuf *gb, uint16_t start, uint16_t len,
                                 uint16_t line_no, int pre_iie, uint8_t cols,
                                 uint8_t gutter, uint8_t *row) {
  const uint8_t *buf = gb->buf;
  uint16_t gs = gb->gap_start;
  uint16_t ge = gb->gap_end;
  uint16_t i = start;
  uint8_t col = gutter;
  uint8_t overflow = 0;
  uint8_t ch;
  fill_row(row, ' ', cols);
  render_gutter(line_no, gutter, row);
  while (i < len) {
    ch = (i < gs) ? buf[i] : buf[(uint16_t)(ge + (i - gs))];
    if (ch == '\n') break;
    if (pre_iie && (ch == '{' || ch == '}' || ch == '|')) {
      uint8_t glyphs[3];
      uint8_t w = expand_glyph(ch, 1, glyphs);
      uint8_t g;
      if ((uint8_t)(col + w) > cols) { overflow = 1; break; }
      for (g = 0; g < w; ++g) row[col++] = glyphs[g];
    } else {
      if (col >= cols) { overflow = 1; break; }
      row[col++] = ch;
    }
    ++i;
  }
  if (overflow) {
    row[cols - 1] = '>';
    while (i < len) { /* advance to the line end for the caller */
      ch = (i < gs) ? buf[i] : buf[(uint16_t)(ge + (i - gs))];
      if (ch == '\n') break;
      ++i;
    }
  }
  return i;
}

/* Cursor's display column within its line, clamped to the last column.
 * Inline reads + width (same hot-path reasoning as render_work_line). */
static uint8_t cursor_display_col(const GapBuf *gb, uint16_t cursor_pos,
                                  int pre_iie, uint8_t cols, uint8_t gutter) {
  const uint8_t *buf = gb->buf;
  uint16_t gs = gb->gap_start;
  uint16_t ge = gb->gap_end;
  uint16_t start;
  uint16_t i;
  uint16_t col;
  uint8_t ch;
  start = textnav_line_start(gb, cursor_pos);
  col = 0;
  for (i = start; i < cursor_pos; ++i) {
    ch = (i < gs) ? buf[i] : buf[(uint16_t)(ge + (i - gs))];
    if (pre_iie && (ch == '{' || ch == '}')) col += 2;
    else if (pre_iie && ch == '|') col += 3;
    else col += 1;
    if (col >= (uint16_t)(cols - gutter)) return (uint8_t)(cols - 1);
  }
  return (uint8_t)(gutter + col); /* shift past the line-number gutter */
}

/* WRAP mode: fill the work area by flowing each logical line (from `top_line`)
 * across as many rows as it needs, `text_w = cols - gutter` display columns
 * per row. Only a line's FIRST row carries the line number; continuation rows
 * have a blank gutter. Sets out->cur_row/cur_col to the cursor's wrapped
 * position (parked at the top if its line is scrolled off). Vertical scroll is
 * by whole logical line (top_line), same as the other modes. */
static void render_work_wrap(const GapBuf *gb, uint16_t top_line,
                             uint16_t cursor_pos, int pre_iie, uint8_t cols,
                             uint8_t gutter, EditorScreen *out) {
  uint16_t len = gapbuf_len(gb);
  uint16_t pos = textnav_line_at(gb, top_line);
  uint16_t line_no = (uint16_t)(top_line + 1);
  uint8_t r = 0;
  out->cur_row = ED_WORK_TOP;      /* default if the cursor's line is off-screen */
  out->cur_col = gutter;
  out->cur_set = 0;                /* editor_render_wrapped sets it once, by line */
#ifdef ED_HAVE_LINE_ROW
  { uint8_t k; for (k = 0; k < ED_WORK_ROWS; ++k) g_ed_line_row[k] = 0xFF; }
#endif
  /* Lay out one logical line per editor_render_wrapped call, flowing it across
   * its wrapped rows; the shared helper also tracks the cursor (so digraph
   * widths are handled) and the cursor-move fast path reuses it. */
  while (r < ED_WORK_ROWS && pos <= len) {
    uint16_t end;
    uint8_t rows;
#ifdef ED_HAVE_LINE_ROW
    /* Record where this logical line starts (index = line_no-1-top_line). */
    g_ed_line_row[(uint8_t)((line_no - 1) - top_line)] = r;
#endif
    rows = editor_render_wrapped(gb, pos, line_no, r, cursor_pos, pre_iie, cols,
                                 gutter, out, &end);
    r = (uint8_t)(r + rows);
    if (end < len) { pos = (uint16_t)(end + 1); ++line_no; } /* past the '\n' */
    else break;                                              /* end of buffer */
  }
  while (r < ED_WORK_ROWS) { fill_row(out->cells[ED_WORK_TOP + r], ' ', cols); ++r; }
}

void editor_render(const GapBuf *gb, uint16_t top_line, uint16_t cursor_pos,
                   uint16_t cursor_line, uint16_t line_count,
                   const char *filename, int dirty, const char *message,
                   int pre_iie, uint8_t cols, uint8_t mode, EditorScreen *out) {
  uint16_t len;
  uint16_t pos;
  uint16_t cursor_col;
  uint8_t gutter;
  uint8_t r;
  len = gapbuf_len(gb);
  cursor_col = textnav_col(gb, cursor_pos);
  gutter = editor_gutter_width(line_count);
  render_status(cursor_line, cursor_col, filename, dirty, cols,
                out->cells[ED_STATUS_ROW]);
  if (mode == ED_MODE_WRAP) {
    render_work_wrap(gb, top_line, cursor_pos, pre_iie, cols, gutter, out);
    fill_row(out->cells[ED_MSG_ROW], ' ', cols);
    put_str(out->cells[ED_MSG_ROW], 0, message, cols);
    return;
  }
  /* TRUNCATE (and, later, HSCROLL): one logical line per row. Single forward
   * pass: find the top line's start once, then walk lines down the screen. */
  pos = textnav_line_at(gb, top_line);
  for (r = 0; r < ED_WORK_ROWS; ++r) {
    uint8_t *row = out->cells[ED_WORK_TOP + r];
    if (pos <= len) {
      uint16_t end = render_work_line(gb, pos, len,
                                      (uint16_t)(top_line + r + 1), pre_iie,
                                      cols, gutter, row);
      pos = (uint16_t)(end + 1); /* next line starts just past the '\n' */
    } else {
      fill_row(row, ' ', cols);
    }
  }
  fill_row(out->cells[ED_MSG_ROW], ' ', cols);
  put_str(out->cells[ED_MSG_ROW], 0, message, cols);
  /* Cursor: place it if its line is on screen, else park at the top-left. */
  if (cursor_line >= top_line &&
      cursor_line < (uint16_t)(top_line + ED_WORK_ROWS)) {
    out->cur_row = (uint8_t)(ED_WORK_TOP + (cursor_line - top_line));
    out->cur_col = cursor_display_col(gb, cursor_pos, pre_iie, cols, gutter);
    out->cur_set = 1;
  } else {
    out->cur_row = ED_WORK_TOP;
    out->cur_col = 0;
    out->cur_set = 0;
  }
}


/* Render ONE logical line (starting at `start`) across as many wrapped rows as
 * it needs, into out->cells from work row `row0` down — the per-line core of the
 * WRAP layout. Tracks the cursor: if `cursor_pos` lands in this line and
 * out->cur_set is still 0, sets out->cur_row/cur_col (so digraph widths are
 * honoured) and out->cur_set. Sets *out_end to the line-end position (the '\n'
 * index, or len). Returns the number of work rows the line filled.
 *
 * Shared by render_work_wrap (called per visible line) and the editor's typing
 * fast path (re-render just the edited line's rows when its row COUNT is
 * unchanged). A line longer than the remaining work area drops its tail (fills
 * to the bottom) — cur_set then stays 0 for a cursor in the dropped part, so the
 * caller scrolls / full-renders. */
uint8_t editor_render_wrapped(const GapBuf *gb, uint16_t start, uint16_t line_no,
                              uint8_t row0, uint16_t cursor_pos, int pre_iie,
                              uint8_t cols, uint8_t gutter, EditorScreen *out,
                              uint16_t *out_end) {
  const uint8_t *buf = gb->buf;
  uint16_t gs = gb->gap_start;
  uint16_t ge = gb->gap_end;
  uint16_t len = gapbuf_len(gb);
  uint8_t text_w = (uint8_t)(cols - gutter);
  uint16_t pos = start;
  uint8_t r = row0;
  uint8_t col = 0;
  uint8_t *row = out->cells[ED_WORK_TOP + r];
  fill_row(row, ' ', cols);
  render_gutter(line_no, gutter, row);     /* only the first row is numbered */
  for (;;) {
    uint8_t ch;
    uint8_t w;
    uint8_t glyphs[3];
    uint8_t g;
    if (pos == cursor_pos && !out->cur_set) {
      out->cur_row = (uint8_t)(ED_WORK_TOP + r);
      out->cur_col = (uint8_t)(gutter + col);
      out->cur_set = 1;
    }
    if (pos >= len) break;
    ch = (pos < gs) ? buf[pos] : buf[(uint16_t)(ge + (pos - gs))];
    if (ch == '\n') break;
    w = expand_glyph(ch, pre_iie, glyphs);
    if ((uint8_t)(col + w) > text_w) {       /* wrap onto the next row */
      if ((uint8_t)(r + 1) >= ED_WORK_ROWS) {/* no room: drop the tail */
        while (pos < len) {
          ch = (pos < gs) ? buf[pos] : buf[(uint16_t)(ge + (pos - gs))];
          if (ch == '\n') break;
          ++pos;
        }
        break;
      }
      ++r;
      col = 0;
      row = out->cells[ED_WORK_TOP + r];
      fill_row(row, ' ', cols);             /* continuation row: blank gutter */
      if (pos == cursor_pos && !out->cur_set) {
        out->cur_row = (uint8_t)(ED_WORK_TOP + r);
        out->cur_col = gutter;
        out->cur_set = 1;
      }
    }
    for (g = 0; g < w; ++g) row[gutter + col + g] = glyphs[g];
    col = (uint8_t)(col + w);
    ++pos;
  }
  *out_end = pos;
  return (uint8_t)(r - row0 + 1);
}

#ifdef ED_HAVE_STATUS
/* Render only the status row into `row` (render_status is private here) — for
 * the fast path's deferred, on-pause status refresh. */
void editor_status(uint16_t cursor_line, uint16_t cursor_col,
                   const char *filename, int dirty, uint8_t cols,
                   uint8_t *row) {
  render_status(cursor_line, cursor_col, filename, dirty, cols, row);
}
#endif
