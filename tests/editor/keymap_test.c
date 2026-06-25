/* Editor key-dispatch unit tests (host).
 *
 * Exercises editor_dispatch's editing, cursor, kill, scroll-follow, and the
 * I/O-key action returns — all without keyboard/screen/ProDOS.
 */
#include <stdint.h>
#include <string.h>

#include "editor/gapbuf.h"
#include "editor/keymap.h"
#include "editor/screen.h"
#include "editor/textnav.h"

#define EXPECT(cond, code) do { if (!(cond)) return (code); } while (0)

static int buf_is(GapBuf *gb, const char *expect) {
  uint8_t out[128];
  uint16_t n = gapbuf_serialize(gb, out, (uint16_t)sizeof out);
  uint16_t want = (uint16_t)strlen(expect);
  if (n != want) return 0;
  return memcmp(out, expect, n) == 0;
}

/* Drive a string through dispatch the way the loop would: a literal '\n'
 * is the Return key (0x0D), which dispatch inserts as a newline. */
static void type(EditorState *st, const char *s) {
  while (*s) {
    uint8_t k = (uint8_t)*s++;
    editor_dispatch(st, k == '\n' ? (uint8_t)ED_KEY_RETURN : k);
  }
}

int test_keymap_insert_and_dirty(void) {
  uint8_t store[64];
  GapBuf gb;
  EditorState st;
  gapbuf_init(&gb, store, (uint16_t)sizeof store);
  editor_state_init(&st, &gb);
  EXPECT(st.dirty == 0, 1);
  type(&st, "let");
  EXPECT(buf_is(&gb, "let"), 2);
  EXPECT(gapbuf_pos(&gb) == 3, 3);
  EXPECT(st.dirty == 1, 4);
  return 0;
}

int test_keymap_backspace(void) {
  uint8_t store[64];
  GapBuf gb;
  EditorState st;
  gapbuf_init(&gb, store, (uint16_t)sizeof store);
  editor_state_init(&st, &gb);
  type(&st, "abc");
  editor_dispatch(&st, ED_KEY_DELETE); /* //e Delete key = backspace */
  EXPECT(buf_is(&gb, "ab"), 1);
  editor_dispatch(&st, ED_KEY_CTRL_D); /* Ctrl-D = backspace (II+ has no Delete key) */
  EXPECT(buf_is(&gb, "a"), 2);
  return 0;
}

int test_keymap_arrows(void) {
  uint8_t store[64];
  GapBuf gb;
  EditorState st;
  gapbuf_init(&gb, store, (uint16_t)sizeof store);
  editor_state_init(&st, &gb);
  type(&st, "abcd"); /* cursor at 4 */
  /* Both arrows are non-destructive motion (Pascal-style): the buffer never
     changes, only the cursor. */
  editor_dispatch(&st, ED_KEY_LEFT);
  EXPECT(buf_is(&gb, "abcd"), 1);
  EXPECT(gapbuf_pos(&gb) == 3, 2);
  editor_dispatch(&st, ED_KEY_LEFT);
  EXPECT(gapbuf_pos(&gb) == 2, 3);
  EXPECT(buf_is(&gb, "abcd"), 4);
  editor_dispatch(&st, ED_KEY_RIGHT);
  EXPECT(gapbuf_pos(&gb) == 3, 5);
  EXPECT(buf_is(&gb, "abcd"), 6);
  /* Up/down across two lines. */
  gapbuf_init(&gb, store, (uint16_t)sizeof store);
  editor_state_init(&st, &gb);
  type(&st, "ab\ncde"); /* cursor at end, on line 1 */
  editor_dispatch(&st, ED_KEY_UP);
  EXPECT(textnav_line_index(&gb, gapbuf_pos(&gb)) == 0, 7);
  editor_dispatch(&st, ED_KEY_DOWN);
  EXPECT(textnav_line_index(&gb, gapbuf_pos(&gb)) == 1, 8);
  return 0;
}

int test_keymap_return_inserts_newline(void) {
  uint8_t store[64];
  GapBuf gb;
  EditorState st;
  gapbuf_init(&gb, store, (uint16_t)sizeof store);
  editor_state_init(&st, &gb);
  type(&st, "a");
  editor_dispatch(&st, ED_KEY_RETURN);
  type(&st, "b");
  EXPECT(buf_is(&gb, "a\nb"), 1);
  EXPECT(textnav_line_count(&gb) == 2, 2);
  return 0;
}

int test_keymap_cursor_moves(void) {
  uint8_t store[64];
  GapBuf gb;
  EditorState st;
  gapbuf_init(&gb, store, (uint16_t)sizeof store);
  editor_state_init(&st, &gb);
  type(&st, "abcd"); /* cursor at 4 */
  editor_dispatch(&st, ED_KEY_LEFT);
  EXPECT(gapbuf_pos(&gb) == 3, 1);
  editor_dispatch(&st, ED_KEY_CTRL_A);
  EXPECT(gapbuf_pos(&gb) == 0, 2);
  editor_dispatch(&st, ED_KEY_RIGHT);
  EXPECT(gapbuf_pos(&gb) == 1, 3);
  editor_dispatch(&st, ED_KEY_CTRL_E);
  EXPECT(gapbuf_pos(&gb) == 4, 4);
  /* Insert mid-line after moving the cursor. */
  editor_dispatch(&st, ED_KEY_CTRL_A);
  editor_dispatch(&st, (uint8_t)'X');
  EXPECT(buf_is(&gb, "Xabcd"), 5);
  return 0;
}

int test_keymap_up_down(void) {
  uint8_t store[64];
  GapBuf gb;
  EditorState st;
  gapbuf_init(&gb, store, (uint16_t)sizeof store);
  editor_state_init(&st, &gb);
  type(&st, "abc\nde\nfghi"); /* 3 lines; cursor at end (line 2, col 4) */
  editor_dispatch(&st, ED_KEY_UP);              /* up to line 1, col clamps */
  EXPECT(textnav_line_index(&gb, gapbuf_pos(&gb)) == 1, 1);
  editor_dispatch(&st, ED_KEY_CTRL_O);          /* Ctrl-O (Pascal) = up to line 0 */
  EXPECT(textnav_line_index(&gb, gapbuf_pos(&gb)) == 0, 2);
  editor_dispatch(&st, ED_KEY_CTRL_L);          /* Ctrl-L (Pascal) = down to line 1 */
  EXPECT(textnav_line_index(&gb, gapbuf_pos(&gb)) == 1, 3);
  editor_dispatch(&st, ED_KEY_DOWN);            /* down arrow = down to line 2 */
  EXPECT(textnav_line_index(&gb, gapbuf_pos(&gb)) == 2, 4);
  return 0;
}

int test_keymap_ctrl_d_backspace(void) {
  uint8_t store[64];
  GapBuf gb;
  EditorState st;
  gapbuf_init(&gb, store, (uint16_t)sizeof store);
  editor_state_init(&st, &gb);
  type(&st, "abc");
  gapbuf_move_to(&gb, 2);               /* between 'b' and 'c' */
  editor_dispatch(&st, ED_KEY_CTRL_D);  /* Ctrl-D backspaces 'b' (to the left) */
  EXPECT(buf_is(&gb, "ac"), 1);
  EXPECT(st.dirty == 1, 2);
  gapbuf_move_to(&gb, 0);               /* at start: nothing to the left */
  editor_dispatch(&st, ED_KEY_CTRL_D);
  EXPECT(buf_is(&gb, "ac"), 3);
  return 0;
}

int test_keymap_io_actions(void) {
  uint8_t store[64];
  GapBuf gb;
  EditorState st;
  gapbuf_init(&gb, store, (uint16_t)sizeof store);
  editor_state_init(&st, &gb);
  type(&st, "x");
  EXPECT(editor_dispatch(&st, ED_KEY_CTRL_S) == ED_ACT_SAVE, 1);
  EXPECT(editor_dispatch(&st, ED_KEY_CTRL_R) == ED_ACT_RUN, 2);
  EXPECT(editor_dispatch(&st, ED_KEY_CTRL_Q) == ED_ACT_QUIT, 3);
  /* I/O keys don't touch the buffer. */
  EXPECT(buf_is(&gb, "x"), 5);
  return 0;
}

int test_keymap_ignores_other_controls(void) {
  uint8_t store[64];
  GapBuf gb;
  EditorState st;
  gapbuf_init(&gb, store, (uint16_t)sizeof store);
  editor_state_init(&st, &gb);
  type(&st, "ok");
  editor_dispatch(&st, 0x03); /* Ctrl-C: not bound -> ignored */
  editor_dispatch(&st, 0x09); /* Tab: not supported -> ignored */
  /* The retired movement aliases (Ctrl-B/F $02/$06, Ctrl-P/N $10/$0E) and the
     removed ESC ($1B) are no longer bound -> ignored, cursor unmoved. */
  editor_dispatch(&st, 0x02);
  editor_dispatch(&st, 0x06);
  editor_dispatch(&st, 0x10);
  editor_dispatch(&st, 0x0E);
  editor_dispatch(&st, 0x1B);
  EXPECT(buf_is(&gb, "ok"), 1);
  EXPECT(gapbuf_pos(&gb) == 2, 2);
  return 0;
}

int test_keymap_page_up_down(void) {
  uint8_t store[256];
  GapBuf gb;
  EditorState st;
  int i;
  gapbuf_init(&gb, store, (uint16_t)sizeof store);
  editor_state_init(&st, &gb);
  /* 30 lines "L0".."L29"; cursor ends on line 29. */
  for (i = 0; i < 30; ++i) {
    char d[4];
    int n = 0;
    d[n++] = 'L';
    if (i >= 10) d[n++] = (char)('0' + i / 10);
    d[n++] = (char)('0' + i % 10);
    d[n] = '\0';
    type(&st, d);
    if (i < 29) editor_dispatch(&st, ED_KEY_RETURN);
  }
  /* Page up jumps a page less 2 lines of overlap (ED_WORK_ROWS-2 = 20): 29 -> 9. */
  editor_dispatch(&st, ED_KEY_PGUP);
  EXPECT(textnav_line_index(&gb, gapbuf_pos(&gb)) == 9, 1);
  /* A second page up clamps at the first line. */
  editor_dispatch(&st, ED_KEY_PGUP);
  EXPECT(textnav_line_index(&gb, gapbuf_pos(&gb)) == 0, 2);
  EXPECT(st.top_line == 0, 3);
  /* Page down jumps 20 lines: 0 -> 20; the viewport follows the cursor. */
  editor_dispatch(&st, ED_KEY_PGDN);
  EXPECT(textnav_line_index(&gb, gapbuf_pos(&gb)) == 20, 4);
  /* A second page down clamps at the last line. */
  editor_dispatch(&st, ED_KEY_PGDN);
  EXPECT(textnav_line_index(&gb, gapbuf_pos(&gb)) == 29, 5);
  return 0;
}

int test_keymap_cook_key(void) {
  /* II+ cooked-mode mapping: Ctrl-W ($17) -> input-method `_` (the II+ keyboard
     has no `_` key). */
  EXPECT(editor_cook_key(0x17) == (uint8_t)'_', 1);
  /* Typed A-Z auto-lowercase to the canonical buffer form. */
  EXPECT(editor_cook_key('A') == 'a', 2);
  EXPECT(editor_cook_key('Z') == 'z', 3);
  /* Already-lowercase and symbols pass through unchanged. */
  EXPECT(editor_cook_key('a') == 'a', 4);
  EXPECT(editor_cook_key('5') == '5', 5);
  EXPECT(editor_cook_key((uint8_t)'\'') == (uint8_t)'\'', 6); /* ' case marker kept */
  /* The arrows are plain cursor motion now and pass through unchanged, as do
     the command keys (Ctrl-E here), Return, and the left arrow / Ctrl-H. */
  EXPECT(editor_cook_key(ED_KEY_RIGHT) == ED_KEY_RIGHT, 7);
  EXPECT(editor_cook_key(ED_KEY_CTRL_E) == ED_KEY_CTRL_E, 8);
  EXPECT(editor_cook_key(ED_KEY_RETURN) == ED_KEY_RETURN, 9);
  EXPECT(editor_cook_key(ED_KEY_LEFT) == ED_KEY_LEFT, 10);
  return 0;
}

int test_keymap_scroll_follows(void) {
  uint8_t store[256];
  GapBuf gb;
  EditorState st;
  int i;
  gapbuf_init(&gb, store, (uint16_t)sizeof store);
  editor_state_init(&st, &gb);
  /* 30 lines "L0".."L29"; cursor ends on line 29. */
  for (i = 0; i < 30; ++i) {
    char d[4];
    int n = 0;
    d[n++] = 'L';
    if (i >= 10) d[n++] = (char)('0' + i / 10);
    d[n++] = (char)('0' + i % 10);
    d[n] = '\0';
    type(&st, d);
    if (i < 29) editor_dispatch(&st, ED_KEY_RETURN);
  }
  /* Last line (29) must be visible in 22 rows -> top = 29-22+1 = 8. */
  EXPECT(st.top_line == 8, 1);
  /* Walk up to the top; the viewport follows back to 0. */
  for (i = 0; i < 29; ++i) editor_dispatch(&st, ED_KEY_UP);
  EXPECT(textnav_line_index(&gb, gapbuf_pos(&gb)) == 0, 2);
  EXPECT(st.top_line == 0, 3);
  return 0;
}
