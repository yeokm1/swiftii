/* Editor key dispatch — the pure heart of the edit loop.
 *
 * editor_dispatch applies one keystroke to the editor state: it mutates the
 * gap buffer (insert / delete / kill), moves the cursor (the gap position),
 * keeps the viewport scrolled to the cursor, and tracks the dirty flag. Keys
 * that need I/O — save, run, quit, open — don't do the I/O here; they return
 * an EditorAction for the platform loop (a later slice) to carry out. That
 * split keeps everything decision-making in this file host-testable, with no
 * keyboard, screen, or ProDOS dependency.
 *
 * Byte-agnostic by design: a printable key is inserted verbatim. On a //e the
 * loop hands canonical Swift bytes; on a //+ it hands raw typed bytes that
 * `input_translate` canonicalises at save time (design doc 003 / 006). This
 * module doesn't care which.
 *
 * The cursor is simply the gap-buffer position (gapbuf_pos); there is no
 * separate cursor field to keep in sync.
 *
 * Pure C90; compiles on host and target.
 */
#ifndef SWIFTII_EDITOR_KEYMAP_H
#define SWIFTII_EDITOR_KEYMAP_H

#include <stdint.h>

#include "gapbuf.h"

typedef struct editor_state {
  GapBuf *gb;
  uint16_t top_line; /* logical line shown on the first work row */
  int dirty;         /* unsaved changes since the last load/save */
} EditorState;

/* What the loop must do after a keystroke. Editing/cursor keys return NONE
 * (handled fully here); I/O keys return their action for the loop. */
typedef enum editor_action {
  ED_ACT_NONE = 0,
  ED_ACT_SAVE,  /* Ctrl-S */
  ED_ACT_RUN,   /* Ctrl-R */
  ED_ACT_QUIT   /* Ctrl-Q (open-by-name was dropped: files open from the browser) */
} EditorAction;

/* Key codes the dispatcher recognises. The Apple arrow keys ARE control
 * codes (left=Ctrl-H, right=Ctrl-U, up=Ctrl-K, down=Ctrl-J), so the same
 * byte means "arrow" and "that Ctrl-letter" — there is no distinguishing
 * them. Following the Apple Pascal (UCSD) editor, the arrows are
 * NON-DESTRUCTIVE motion: left arrow / Ctrl-H ($08) moves the cursor left,
 * right arrow / Ctrl-U ($15) moves it right. Deletion is a separate key:
 * Ctrl-D ($04) backspaces (deletes the char to the left), as does the //e
 * Delete key ($7F) — the II+ has no $7F Delete key, so Ctrl-D is its backspace.
 * (Forward-delete was dropped: with motion + backspace it is just → then
 * Ctrl-D.) On the pre-IIe editor loop, cooked Swift mode maps Ctrl-W ($17) to
 * the input-method `_`. A real //+ has no up/down arrow keys, so up/down are
 * Ctrl-O / Ctrl-L (the Apple Pascal convention — O sits directly above L),
 * plus Ctrl-K / Ctrl-J (the arrow byte codes, = the //e up/down arrows). (The
 * emacs-style Ctrl-B/F/P/N aliases and the modal ESC-IJKM diamond were
 * removed; in-editor open-by-name was dropped too, which freed Ctrl-O — files
 * open from the browser.) */
#define ED_KEY_CTRL_A   0x01
#define ED_KEY_CTRL_D   0x04  /* backspace (delete-left); II+ has no $7F Delete key */
#define ED_KEY_CTRL_E   0x05
#define ED_KEY_LEFT     0x08  /* left arrow / Ctrl-H -> move left */
#define ED_KEY_DOWN     0x0A  /* down arrow / Ctrl-J -> move down */
#define ED_KEY_UP       0x0B  /* up arrow / Ctrl-K -> move up */
#define ED_KEY_CTRL_L   0x0C  /* move down (Apple Pascal) */
#define ED_KEY_RETURN   0x0D
#define ED_KEY_CTRL_O   0x0F  /* move up (Apple Pascal) */
#define ED_KEY_CTRL_Q   0x11
#define ED_KEY_CTRL_R   0x12
#define ED_KEY_CTRL_S   0x13
#define ED_KEY_PGUP     0x14  /* Ctrl-T: scroll up one page */
#define ED_KEY_RIGHT    0x15  /* right arrow / Ctrl-U -> move right */
#define ED_KEY_PGDN     0x16  /* Ctrl-V: scroll down one page */
#define ED_KEY_DELETE   0x7F  /* //e Delete key -> backspace */

/// Bind the state to a gap buffer with the cursor and viewport at the top
/// and no unsaved changes.
void editor_state_init(EditorState *st, GapBuf *gb);

/// Apply one keystroke. Returns the action the loop must perform (NONE for
/// editing/cursor keys, which are handled here).
EditorAction editor_dispatch(EditorState *st, uint8_t key);

/// II+ "cooked" (Swift digraph) input mapping, applied to a typed key before
/// it reaches editor_dispatch on a pre-IIe build. The ][+ keyboard is
/// uppercase-only and has no `_` key, so cooked mode maps Ctrl-W ($17) to the
/// input-method underscore, and typed A-Z auto-lowercase to the canonical
/// buffer form (uppercase is entered via the ' case marker, resolved at save).
/// Any other key — including the arrows ($08/$15), now plain cursor motion —
/// is returned unchanged. Caller applies this only when cooked mode is on.
uint8_t editor_cook_key(uint8_t key);

#endif /* SWIFTII_EDITOR_KEYMAP_H */
