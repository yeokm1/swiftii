/* End-to-end editor session tests (host).
 *
 * The other editor tests each exercise one portable module in isolation:
 * keymap_test drives editor_dispatch, screen_test drives editor_render,
 * fileio_test drives the load/save round trip. These tests stitch the whole
 * chain together the way the on-target loop does — a scripted keypress
 * sequence flows through editor_dispatch (and, for the //+ path, through
 * editor_cook_key first) into the gap buffer, then out through the host file
 * layer (stdio standing in for ProDOS MLI — the "mock-ProDOS layer") and back,
 * and the result is asserted as both the final buffer text AND a rendered
 * screen snapshot.
 *
 * Two of them go one step further (the integration round trip): they save the
 * authored program and run it through the real file-mode driver
 * (file_runner_run) with stdout captured, proving that the bytes the editor
 * writes actually compile and produce the expected output. That is the
 * edit -> save -> run path the user takes, end to end, on the host.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "editor/gapbuf.h"
#include "editor/keymap.h"
#include "editor/screen.h"
#include "editor/textnav.h"
#include "editor/fileio.h"
#include "file_runner/file_runner.h"
#include "platform/platform.h"

#define EXPECT(cond, code) do { if (!(cond)) return (code); } while (0)

#define TMP "/tmp/swiftii_editor_session_test.swift"

/* Drive a string through dispatch the way the //e loop would (canonical bytes):
   a literal '\n' is the Return key, which dispatch inserts as a newline. */
static void type(EditorState *st, const char *s) {
  while (*s) {
    uint8_t k = (uint8_t)*s++;
    editor_dispatch(st, k == '\n' ? (uint8_t)ED_KEY_RETURN : k);
  }
}

/* Drive a string through the //+ "cooked" path: every raw typed byte passes
   through editor_cook_key (uppercase auto-lowercases, Ctrl-W $17 -> `_`) before
   dispatch, exactly as the pre-IIe editor loop does. */
static void cooked_type(EditorState *st, const char *s) {
  while (*s) editor_dispatch(st, editor_cook_key((uint8_t)*s++));
}

static int buf_is(GapBuf *gb, const char *expect) {
  uint8_t out[256];
  uint16_t n = gapbuf_serialize(gb, out, (uint16_t)sizeof out);
  uint16_t want = (uint16_t)strlen(expect);
  return n == want && memcmp(out, expect, n) == 0;
}

/* Read a whole file into out (NUL-terminated); returns byte count, or -1. */
static int slurp(const char *path, char *out, int cap) {
  FILE *f = fopen(path, "rb");
  int n;
  if (!f) return -1;
  n = (int)fread(out, 1, (size_t)(cap - 1), f);
  fclose(f);
  if (n < 0) return -1;
  out[n] = '\0';
  return n;
}

/* The text of one work row (skip the line-number gutter), trailing spaces
   stripped — the same shape screen_test asserts on. */
static void work_text(const EditorScreen *sc, uint8_t work_row, uint8_t gutter,
                      char *out) {
  uint8_t r = (uint8_t)(ED_WORK_TOP + work_row);
  int n = ED_COLS;
  int i;
  while (n > gutter && sc->cells[r][n - 1] == ' ') --n;
  for (i = gutter; i < n; ++i) out[i - gutter] = (char)sc->cells[r][i];
  out[n > gutter ? n - gutter : 0] = '\0';
}

/* Render the buffer the way the editor does (native //e widths, 40 cols,
   truncate mode), deriving the cached cursor line / line count. */
static void render(GapBuf *gb, const char *fn, EditorScreen *out) {
  uint16_t cur = gapbuf_pos(gb);
  editor_render(gb, 0, cur, textnav_line_index(gb, cur),
                textnav_line_count(gb), fn, 0, NULL, 0, ED_COLS,
                ED_MODE_TRUNCATE, out);
}

/* Run the file at `path` through the real file-mode driver with stdout
   captured into `out` (dup2, the same way compiler_test.c captures). Returns 0
   on a clean run, negative on a harness error or a non-zero driver result. */
static int run_file_capture(const char *path, char *out, size_t out_cap) {
  int saved_fd, capture_fd, frc;
  ssize_t n;
  const char *cap_path = "/tmp/swiftii_editor_session_out.txt";

  fflush(stdout);
  saved_fd = dup(1);
  if (saved_fd < 0) return -2;
  capture_fd = open(cap_path, O_RDWR | O_CREAT | O_TRUNC, 0600);
  if (capture_fd < 0) { close(saved_fd); return -3; }
  if (dup2(capture_fd, 1) < 0) {
    close(saved_fd);
    close(capture_fd);
    return -4;
  }
  close(capture_fd);

  platform_init();
  frc = file_runner_run(path);
  platform_shutdown();
  fflush(stdout);

  dup2(saved_fd, 1);
  close(saved_fd);

  if (frc != 0) return -5;

  capture_fd = open(cap_path, O_RDONLY);
  if (capture_fd < 0) return -6;
  n = read(capture_fd, out, out_cap - 1);
  close(capture_fd);
  if (n < 0) return -7;
  out[n] = '\0';
  return 0;
}

/* Scripted session: type a two-line program, save it through the host file
   layer, reload into a fresh buffer, and assert both the round-tripped buffer
   and the rendered screen snapshot (work rows + cursor home). */
int test_session_type_save_load_render(void) {
  uint8_t store[256];
  GapBuf gb;
  EditorState st;
  EditorScreen sc;
  char text[ED_COLS + 1];
  uint8_t gw;

  gapbuf_init(&gb, store, (uint16_t)sizeof store);
  editor_state_init(&st, &gb);
  EXPECT(st.dirty == 0, 1);
  type(&st, "let x = 1");
  editor_dispatch(&st, ED_KEY_RETURN);
  type(&st, "print(x)");
  EXPECT(st.dirty == 1, 2);
  EXPECT(buf_is(&gb, "let x = 1\nprint(x)"), 3);

  /* Save out and back via the mock-ProDOS (host stdio) file layer. */
  EXPECT(editor_file_save(&gb, TMP, 0) == EFIO_OK, 4);
  gapbuf_init(&gb, store, (uint16_t)sizeof store);
  EXPECT(editor_file_load(&gb, TMP, 0) == EFIO_OK, 5);
  EXPECT(buf_is(&gb, "let x = 1\nprint(x)"), 6);
  EXPECT(gapbuf_pos(&gb) == 0, 7); /* cursor home after load */

  /* Screen snapshot: each logical line on its own work row, after the gutter. */
  render(&gb, "greet.swift", &sc);
  gw = editor_gutter_width(textnav_line_count(&gb));
  work_text(&sc, 0, gw, text);
  EXPECT(strcmp(text, "let x = 1") == 0, 8);
  work_text(&sc, 1, gw, text);
  EXPECT(strcmp(text, "print(x)") == 0, 9);
  /* The cursor sits on the first work row, in the first text column. */
  EXPECT(sc.cur_set == 1, 10);
  EXPECT(sc.cur_row == ED_WORK_TOP, 11);
  EXPECT(sc.cur_col == gw, 12);
  return 0;
}

/* The //+ cooked-input session: raw uppercase keystrokes auto-lowercase, the
   Ctrl-W ($17) key inserts the underscore, and literal `<% %>` digraphs canonicalise to
   braces only at save time — so the live buffer keeps the typed form while the
   bytes on disk are canonical lowercase Swift. */
int test_session_iiplus_cooked_keystrokes(void) {
  uint8_t store[256];
  GapBuf gb;
  EditorState st;
  char disk[128];

  gapbuf_init(&gb, store, (uint16_t)sizeof store);
  editor_state_init(&st, &gb);
  cooked_type(&st, "FUNC F() <%");
  editor_dispatch(&st, editor_cook_key(ED_KEY_RETURN));
  cooked_type(&st, "LET N");
  editor_dispatch(&st, editor_cook_key(0x17)); /* Ctrl-W -> `_` */
  cooked_type(&st, "X = 1");
  editor_dispatch(&st, editor_cook_key(ED_KEY_RETURN));
  cooked_type(&st, "%>");

  /* Live buffer: lowercased letters, the `_` from Ctrl-W, literal digraphs. */
  EXPECT(buf_is(&gb, "func f() <%\nlet n_x = 1\n%>"), 1);

  /* A cooked save canonicalises `<% %>` to `{ }` on disk. */
  EXPECT(editor_file_save(&gb, TMP, 1) == EFIO_OK, 2);
  EXPECT(slurp(TMP, disk, (int)sizeof disk) >= 0, 3);
  EXPECT(strcmp(disk, "func f() {\nlet n_x = 1\n}") == 0, 4);
  return 0;
}

/* edit -> save -> run: author a program with keystrokes, save it, and run it
   through the real file-mode driver; the captured output must match. */
int test_session_edit_save_run(void) {
  uint8_t store[128];
  GapBuf gb;
  EditorState st;
  char out[64];

  gapbuf_init(&gb, store, (uint16_t)sizeof store);
  editor_state_init(&st, &gb);
  type(&st, "print(6 * 7)");
  EXPECT(editor_file_save(&gb, TMP, 0) == EFIO_OK, 1);
  EXPECT(run_file_capture(TMP, out, sizeof out) == 0, 2);
  EXPECT(strcmp(out, "42\n") == 0, 3);
  return 0;
}

/* Same round trip with a mid-authoring correction: a fat-fingered digit is
   removed with the Ctrl-D backspace before the line is saved and run,
   so the edit (not just the typing) is what reaches the file. */
int test_session_backspace_correction_run(void) {
  uint8_t store[128];
  GapBuf gb;
  EditorState st;
  char out[64];

  gapbuf_init(&gb, store, (uint16_t)sizeof store);
  editor_state_init(&st, &gb);
  type(&st, "print(6 * ");
  editor_dispatch(&st, (uint8_t)'8');     /* oops */
  editor_dispatch(&st, ED_KEY_CTRL_D);    /* backspace removes the 8 */
  type(&st, "7)");
  EXPECT(buf_is(&gb, "print(6 * 7)"), 1);
  EXPECT(editor_file_save(&gb, TMP, 0) == EFIO_OK, 2);
  EXPECT(run_file_capture(TMP, out, sizeof out) == 0, 3);
  EXPECT(strcmp(out, "42\n") == 0, 4);
  return 0;
}
