/* Editor screen renderer unit tests (host).
 *
 * Asserts on the pure 24x40 grid editor_render builds — work-row contents,
 * cursor placement, display-width budgeting (native vs pre-IIe), overflow
 * truncation, vertical scroll, and the status/message lines.
 *
 * The line-number gutter is dynamic (editor_gutter_width: the largest line
 * number's digit count plus a trailing space), so the tests derive the gutter
 * for each buffer via gut() rather than assuming a fixed width.
 */
#include <stdint.h>
#include <string.h>

#include "editor/gapbuf.h"
#include "editor/screen.h"
#include "editor/textnav.h"

#define EXPECT(cond, code) do { if (!(cond)) return (code); } while (0)

static void load(GapBuf *gb, uint8_t *store, uint16_t cap, const char *s) {
  uint16_t i;
  gapbuf_init(gb, store, cap);
  for (i = 0; s[i]; ++i) gapbuf_insert(gb, (uint8_t)s[i]);
}

/* The gutter width editor_render will use for this buffer. */
static uint8_t gut(const GapBuf *gb) {
  return editor_gutter_width(textnav_line_count(gb));
}

/* editor_render now takes caller-cached cursor_line / line_count (the editor
 * keeps them current); these tests want the true values, so wrap the call and
 * derive them from gb + cursor_pos. */
static void ed_render_t(GapBuf *gb, uint16_t top, uint16_t cur,
                        const char *fn, int dirty, const char *msg,
                        int pre_iie, uint8_t cols, uint8_t mode,
                        EditorScreen *out) {
  editor_render(gb, top, cur, textnav_line_index(gb, cur),
                textnav_line_count(gb), fn, dirty, msg, pre_iie, cols, mode,
                out);
}

/* The text part of a work row (skip the `g`-column line-number gutter),
 * trailing spaces stripped. */
static void text_trim(const EditorScreen *sc, uint8_t r, uint8_t g, char *out) {
  int n = ED_COLS;
  int i;
  while (n > g && sc->cells[r][n - 1] == ' ') --n;
  for (i = g; i < n; ++i) out[i - g] = (char)sc->cells[r][i];
  out[n > g ? n - g : 0] = '\0';
}

int test_screen_glyph_width(void) {
  EXPECT(editor_glyph_width('a', 0) == 1, 1);
  EXPECT(editor_glyph_width('{', 0) == 1, 2); /* native //e */
  EXPECT(editor_glyph_width('{', 1) == 2, 3); /* pre-IIe <% */
  EXPECT(editor_glyph_width('}', 1) == 2, 4);
  EXPECT(editor_glyph_width('|', 1) == 3, 5); /* ??! */
  EXPECT(editor_glyph_width('x', 1) == 1, 6);
  return 0;
}

int test_screen_gutter_width(void) {
  EXPECT(editor_gutter_width(0) == 2, 1);     /* empty -> 1 digit + space */
  EXPECT(editor_gutter_width(1) == 2, 2);
  EXPECT(editor_gutter_width(9) == 2, 3);
  EXPECT(editor_gutter_width(10) == 3, 4);
  EXPECT(editor_gutter_width(99) == 3, 5);
  EXPECT(editor_gutter_width(100) == 4, 6);
  EXPECT(editor_gutter_width(1000) == 5, 7);
  return 0;
}

int test_screen_basic_render(void) {
  uint8_t store[64];
  GapBuf gb;
  EditorScreen sc;
  char buf[ED_COLS + 1];
  uint8_t g;
  load(&gb, store, (uint16_t)sizeof store, "let x = 1\nlet y = 2");
  g = gut(&gb);                                  /* 2 lines -> gutter 2 */
  ed_render_t(&gb, 0, 0, "T.SWIFT", 0, "", 0, ED_COLS, ED_MODE_TRUNCATE, &sc);
  text_trim(&sc, ED_WORK_TOP, g, buf);
  EXPECT(strcmp(buf, "let x = 1") == 0, 1);
  text_trim(&sc, ED_WORK_TOP + 1, g, buf);
  EXPECT(strcmp(buf, "let y = 2") == 0, 2);
  EXPECT(sc.cur_row == ED_WORK_TOP, 3);
  EXPECT(sc.cur_col == g, 4); /* cursor sits past the gutter */
  /* Gutter shows the 1-based line number, units digit at col g-2. */
  EXPECT(sc.cells[ED_WORK_TOP][g - 2] == '1', 5);
  EXPECT(sc.cells[ED_WORK_TOP + 1][g - 2] == '2', 6);
  /* Empty work rows below the text are blank. */
  text_trim(&sc, ED_WORK_TOP + 2, g, buf);
  EXPECT(buf[0] == '\0', 7);
  return 0;
}

int test_screen_cursor_column(void) {
  uint8_t store[64];
  GapBuf gb;
  EditorScreen sc;
  uint8_t g;
  load(&gb, store, (uint16_t)sizeof store, "let x = 1\nlet y = 2");
  g = gut(&gb);
  /* Cursor at column 4 of line 0 (display col = gutter + 4). */
  ed_render_t(&gb, 0, 4, "T", 0, "", 0, ED_COLS, ED_MODE_TRUNCATE, &sc);
  EXPECT(sc.cur_row == ED_WORK_TOP, 1);
  EXPECT(sc.cur_col == g + 4, 2);
  /* Cursor at start of line 1 (pos 10). */
  ed_render_t(&gb, 0, 10, "T", 0, "", 0, ED_COLS, ED_MODE_TRUNCATE, &sc);
  EXPECT(sc.cur_row == ED_WORK_TOP + 1, 3);
  EXPECT(sc.cur_col == g, 4);
  return 0;
}

int test_screen_digraph_width(void) {
  uint8_t store[64];
  GapBuf gb;
  EditorScreen sc;
  char buf[ED_COLS + 1];
  uint8_t g;
  load(&gb, store, (uint16_t)sizeof store, "if x {");
  g = gut(&gb);
  /* Native: brace is one column, cursor at end is text col 6. */
  ed_render_t(&gb, 0, 6, "T", 0, "", 0, ED_COLS, ED_MODE_TRUNCATE, &sc);
  text_trim(&sc, ED_WORK_TOP, g, buf);
  EXPECT(strcmp(buf, "if x {") == 0, 1);
  EXPECT(sc.cur_col == g + 6, 2);
  /* Pre-IIe: brace expands to <% (2 cols); cursor at end is text col 7. */
  ed_render_t(&gb, 0, 6, "T", 0, "", 1, ED_COLS, ED_MODE_TRUNCATE, &sc);
  text_trim(&sc, ED_WORK_TOP, g, buf);
  EXPECT(strcmp(buf, "if x <%") == 0, 3);
  EXPECT(sc.cur_col == g + 7, 4);
  return 0;
}

int test_screen_overflow_marker(void) {
  uint8_t store[64];
  GapBuf gb;
  EditorScreen sc;
  uint8_t g;
  int i;
  load(&gb, store, (uint16_t)sizeof store, "");
  for (i = 0; i < 45; ++i) gapbuf_insert(&gb, (uint8_t)'a');
  g = gut(&gb);
  ed_render_t(&gb, 0, 45, "T", 0, "", 0, ED_COLS, ED_MODE_TRUNCATE, &sc);
  /* Text fills from the gutter to a '>' marker in the last column. */
  EXPECT(sc.cells[ED_WORK_TOP][g] == 'a', 1);
  EXPECT(sc.cells[ED_WORK_TOP][ED_COLS - 2] == 'a', 2);
  EXPECT(sc.cells[ED_WORK_TOP][ED_COLS - 1] == '>', 3);
  /* Cursor past the visible tail clamps to the last column. */
  EXPECT(sc.cur_col == ED_COLS - 1, 4);
  return 0;
}

int test_screen_vertical_scroll(void) {
  uint8_t store[256];
  GapBuf gb;
  EditorScreen sc;
  char buf[ED_COLS + 1];
  uint16_t top;
  uint16_t pos25;
  uint8_t g;
  int i;
  gapbuf_init(&gb, store, (uint16_t)sizeof store);
  /* 30 lines: "L0".."L29", newline-separated. */
  for (i = 0; i < 30; ++i) {
    char tmp[8];
    int j;
    int n;
    n = 0;
    tmp[n++] = 'L';
    if (i >= 10) tmp[n++] = (char)('0' + i / 10);
    tmp[n++] = (char)('0' + i % 10);
    for (j = 0; j < n; ++j) gapbuf_insert(&gb, (uint8_t)tmp[j]);
    if (i < 29) gapbuf_insert(&gb, (uint8_t)'\n');
  }
  g = gut(&gb);                                  /* 30 lines -> gutter 3 */
  pos25 = textnav_line_at(&gb, 25);
  top = editor_scroll(&gb, 0, pos25);
  EXPECT(top == 4, 1); /* line 25 must be visible in 22 rows -> top = 4 */
  ed_render_t(&gb, top, pos25, "T", 0, "", 0, ED_COLS, ED_MODE_TRUNCATE, &sc);
  /* Line 25 sits on the last work row. */
  EXPECT(sc.cur_row == ED_WORK_TOP + (25 - 4), 2);
  text_trim(&sc, sc.cur_row, g, buf);
  EXPECT(strcmp(buf, "L25") == 0, 3);
  /* First work row shows line 4, gutter numbered 5 (1-based). */
  text_trim(&sc, ED_WORK_TOP, g, buf);
  EXPECT(strcmp(buf, "L4") == 0, 4);
  EXPECT(sc.cells[ED_WORK_TOP][g - 2] == '5', 5);
  return 0;
}

int test_screen_status_and_message(void) {
  uint8_t store[64];
  GapBuf gb;
  EditorScreen sc;
  load(&gb, store, (uint16_t)sizeof store, "a\nb\nc");
  /* Cursor on line 2 (pos 4 = 'c'), dirty, with a message. */
  ed_render_t(&gb, 0, 4, "TEST.SWIFT", 1, "FIND: ", 0, ED_COLS, ED_MODE_TRUNCATE, &sc);
  EXPECT(sc.cells[ED_STATUS_ROW][0] == '*', 1);            /* dirty flag */
  EXPECT(sc.cells[ED_STATUS_ROW][1] == 'T', 2);            /* filename */
  /* "L3 C1  EDITED" right-justified (line 3, col 1, unsaved). */
  EXPECT(memcmp(&sc.cells[ED_STATUS_ROW][ED_COLS - 13], "L3 C1  EDITED", 13) == 0, 3);
  EXPECT(memcmp(&sc.cells[ED_MSG_ROW][0], "FIND: ", 6) == 0, 4);
  /* Not dirty -> leading space + "SAVED"; cursor back at line 1 col 1. */
  ed_render_t(&gb, 0, 0, "TEST.SWIFT", 0, "", 0, ED_COLS, ED_MODE_TRUNCATE, &sc);
  EXPECT(sc.cells[ED_STATUS_ROW][0] == ' ', 5);
  EXPECT(memcmp(&sc.cells[ED_STATUS_ROW][ED_COLS - 12], "L1 C1  SAVED", 12) == 0, 6);
  return 0;
}

int test_screen_status_cursor_column(void) {
  uint8_t store[64];
  GapBuf gb;
  EditorScreen sc;
  load(&gb, store, (uint16_t)sizeof store, "hello world");
  /* Cursor at byte 6 of the single line -> column 7 (1-based). */
  ed_render_t(&gb, 0, 6, "T", 0, "", 0, ED_COLS, ED_MODE_TRUNCATE, &sc);
  EXPECT(memcmp(&sc.cells[ED_STATUS_ROW][ED_COLS - 12], "L1 C7  SAVED", 12) == 0, 1);
  return 0;
}

int test_screen_empty_buffer(void) {
  uint8_t store[16];
  GapBuf gb;
  EditorScreen sc;
  char buf[ED_COLS + 1];
  uint8_t g;
  gapbuf_init(&gb, store, (uint16_t)sizeof store);
  g = gut(&gb);
  ed_render_t(&gb, 0, 0, "NEW.SWIFT", 0, "", 0, ED_COLS, ED_MODE_TRUNCATE, &sc);
  text_trim(&sc, ED_WORK_TOP, g, buf);
  EXPECT(buf[0] == '\0', 1);
  EXPECT(sc.cur_row == ED_WORK_TOP, 2);
  EXPECT(sc.cur_col == g, 3);
  /* Even an empty buffer shows line 1 in the gutter. */
  EXPECT(sc.cells[ED_WORK_TOP][g - 2] == '1', 4);
  EXPECT(memcmp(&sc.cells[ED_STATUS_ROW][ED_COLS - 12], "L1 C1  SAVED", 12) == 0, 5);
  return 0;
}

/* WRAP mode: a long logical line flows onto continuation rows; the next
 * logical line follows below the wrap, and the cursor tracks the wrapped pos. */
int test_screen_wrap(void) {
  uint8_t store[128];
  GapBuf gb;
  EditorScreen sc;
  char buf[ED_COLS + 1];
  uint16_t i;
  uint8_t g;
  uint8_t tw;
  gapbuf_init(&gb, store, (uint16_t)sizeof store);
  for (i = 0; i < 50; ++i) gapbuf_insert(&gb, 'a');     /* one 50-char line */
  g = gut(&gb);                                  /* 1 line -> gutter 2 */
  tw = (uint8_t)(ED_COLS - g);                   /* text columns per row */
  ed_render_t(&gb, 0, 50, "T", 0, "", 0, ED_COLS, ED_MODE_WRAP, &sc);
  text_trim(&sc, ED_WORK_TOP, g, buf);
  EXPECT(strlen(buf) == tw, 1);                  /* first `tw` chars on row 0 */
  text_trim(&sc, ED_WORK_TOP + 1, g, buf);
  EXPECT(strlen(buf) == (size_t)(50 - tw), 2);   /* remainder on row 1 */
  EXPECT(sc.cur_row == ED_WORK_TOP + 1, 3);      /* cursor wrapped to row 1 */
  EXPECT(sc.cur_col == g + (50 - tw), 4);
  /* The continuation row has no line number (only the first row does). */
  EXPECT(sc.cells[ED_WORK_TOP + 1][g - 2] == ' ', 5);
  /* A following logical line lands below the wrapped rows. */
  gapbuf_insert(&gb, '\n');
  gapbuf_insert(&gb, 'b');
  ed_render_t(&gb, 0, 0, "T", 0, "", 0, ED_COLS, ED_MODE_WRAP, &sc);
  text_trim(&sc, ED_WORK_TOP + 2, gut(&gb), buf);
  EXPECT(strcmp(buf, "b") == 0, 6);
  return 0;
}

/* WRAP-mode scroll: long lines that each take 2 display rows mean fewer than
 * ED_WORK_ROWS logical lines fit, so the cursor's line can sit below the view
 * while the plain (one-row-per-line) editor_scroll never fires. The editor's
 * fix re-renders one line lower while cur_set stays 0; mirror that loop here. */
int test_screen_scroll_wrap(void) {
  uint8_t store[1600];
  GapBuf gb;
  EditorScreen sc;
  uint16_t i, j;
  uint16_t cur;
  uint16_t top;
  uint8_t g;
  gapbuf_init(&gb, store, (uint16_t)sizeof store);
  /* 20 lines of 70 'a' (gutter 3 -> text_w 37 -> each wraps to exactly 2 rows). */
  for (i = 0; i < 20; ++i) {
    for (j = 0; j < 70; ++j) gapbuf_insert(&gb, 'a');
    if (i < 19) gapbuf_insert(&gb, '\n');
  }
  g = gut(&gb);                                   /* 20 lines -> gutter 3 */
  cur = textnav_line_at(&gb, 15);                 /* cursor at the start of line 15 */
  /* Plain scroll thinks line 15 (< 0 + 22) is on screen and does not move. */
  EXPECT(editor_scroll(&gb, 0, cur) == 0, 1);
  /* At top 0 the cursor's line falls off the bottom -> cur_set 0. */
  ed_render_t(&gb, 0, cur, "T", 0, "", 0, ED_COLS, ED_MODE_WRAP, &sc);
  EXPECT(sc.cur_set == 0, 2);
  /* The editor's loop: drop the top one line until the cursor lands. */
  top = 0;
  for (;;) {
    ed_render_t(&gb, top, cur, "T", 0, "", 0, ED_COLS, ED_MODE_WRAP, &sc);
    if (sc.cur_set || top >= 15) break;
    ++top;
  }
  /* lines 5..15 = 11 lines x 2 rows = 22 rows = the work area, so top = 5. */
  EXPECT(top == 5, 3);
  EXPECT(sc.cur_set == 1, 4);
  EXPECT(sc.cur_row == ED_WORK_TOP + 20, 5);      /* line 15 -> 10 lines x 2 rows down */
  EXPECT(sc.cur_col == g, 6);
  return 0;
}
