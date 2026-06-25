/* Editor key dispatch — see keymap.h.
 *
 * Cursor moves go through textnav (logical lines), then gapbuf_move_to; the
 * scroll is recomputed after every key so the cursor stays on screen. */
#include "keymap.h"

#include "textnav.h"
#include "screen.h"

/* Lines a page key moves the cursor: one full work area less two rows, so a
 * couple of lines from the previous screen stay visible as context. */
#define ED_PAGE_LINES (ED_WORK_ROWS - 2)

void editor_state_init(EditorState *st, GapBuf *gb) {
  st->gb = gb;
  st->top_line = 0;
  st->dirty = 0;
}

/* Keep the viewport tracking the cursor after a move or edit. */
static void follow_cursor(EditorState *st) {
  st->top_line = editor_scroll(st->gb, st->top_line, gapbuf_pos(st->gb));
}

EditorAction editor_dispatch(EditorState *st, uint8_t key) {
  GapBuf *gb = st->gb;
  switch (key) {
    case ED_KEY_CTRL_S: return ED_ACT_SAVE;
    case ED_KEY_CTRL_R: return ED_ACT_RUN;
    case ED_KEY_CTRL_Q: return ED_ACT_QUIT;

    case ED_KEY_DELETE: /* //e Delete key ($7F): backspace */
    case ED_KEY_CTRL_D: /* Ctrl-D: backspace — the II+ has no $7F Delete key,
                         * and its ← is now a non-destructive move (below). */
      if (gapbuf_delete_left(gb)) st->dirty = 1;
      break;
    case ED_KEY_RETURN:
      if (gapbuf_insert(gb, (uint8_t)'\n')) st->dirty = 1;
      break;

    case ED_KEY_LEFT:   /* left arrow / Ctrl-H: move left */
      gapbuf_move_left(gb, 1);
      break;
    case ED_KEY_RIGHT:  /* right arrow / Ctrl-U: move right */
      gapbuf_move_right(gb, 1);
      break;
    case ED_KEY_UP:     /* up arrow / Ctrl-K / Ctrl-O: move up */
    case ED_KEY_CTRL_O:                 /* Ctrl-O = up (Apple Pascal convention) */
      gapbuf_move_to(gb, textnav_up(gb, gapbuf_pos(gb)));
      break;
    case ED_KEY_DOWN:   /* down arrow / Ctrl-J / Ctrl-L: move down */
    case ED_KEY_CTRL_L:                 /* Ctrl-L = down (Apple Pascal convention) */
      gapbuf_move_to(gb, textnav_down(gb, gapbuf_pos(gb)));
      break;
    case ED_KEY_PGUP: { /* Ctrl-T: cursor up one page (viewport follows) */
      uint16_t pos = gapbuf_pos(gb);
      uint8_t i;
      for (i = 0; i < ED_PAGE_LINES; ++i) pos = textnav_up(gb, pos);
      gapbuf_move_to(gb, pos);
      break;
    }
    case ED_KEY_PGDN: { /* Ctrl-V: cursor down one page (viewport follows) */
      uint16_t pos = gapbuf_pos(gb);
      uint8_t i;
      for (i = 0; i < ED_PAGE_LINES; ++i) pos = textnav_down(gb, pos);
      gapbuf_move_to(gb, pos);
      break;
    }
    case ED_KEY_CTRL_A:
      gapbuf_move_to(gb, textnav_line_start(gb, gapbuf_pos(gb)));
      break;
    case ED_KEY_CTRL_E:
      gapbuf_move_to(gb, textnav_line_end(gb, gapbuf_pos(gb)));
      break;

    default:
      /* Printable ASCII -> insert verbatim; ignore other control bytes. */
      if (key >= 0x20 && key < 0x7F) {
        if (gapbuf_insert(gb, key)) st->dirty = 1;
      }
      break;
  }
  follow_cursor(st);
  return ED_ACT_NONE;
}

uint8_t editor_cook_key(uint8_t key) {
  if (key == 0x17) return (uint8_t)'_';            /* Ctrl-W -> input-method underscore */
  if (key >= 'A' && key <= 'Z') return (uint8_t)(key + 32); /* auto-lowercase */
  return key;
}
