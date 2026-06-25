/* src/editor/editor.h — entry point for the on-target text editor.
 *
 * Since the merge the editor is not a standalone SYS binary but a
 * subsystem of the boot launcher (one SWIFTII.SYSTEM). The launcher's file
 * browser calls editor_main() in-process and it returns when the user quits
 * (Ctrl-Q) — no cold reboot, because both kept ProDOS MLI resident.
 */
#ifndef SWIFTII_EDITOR_H
#define SWIFTII_EDITOR_H

#include <stdint.h>

/* editor_main return codes. */
#define EDITOR_QUIT 0   /* Ctrl-Q: returned to the caller; redraw the browser. */
#define EDITOR_RUN  1   /* Ctrl-R: caller should run editor_saved_path().       */

/* Run the editor on `path` (an absolute ProDOS path), or on a new scratch
 * buffer when `path` is NULL or empty. `start_cursor` is the initial cursor
 * byte offset into the loaded buffer (0 = top); used to restore the position after
 * an edit -> run -> edit round-trip (see editor_saved_cursor). It is clamped to
 * the buffer length and ignored when no file loads. Returns EDITOR_QUIT when the
 * user quits back to the caller (the file browser) — redraw your own UI — or
 * EDITOR_RUN when the user hit Ctrl-R: the buffer was saved and the caller
 * should run the file named by editor_saved_path().
 *
 * NOTE: the editor's gap buffer lives in low RAM ($0800-$1BFF, the GAPBUF
 * segment), which the file browser also uses for its directory tables. The two
 * are time-disjoint; `path` must therefore point OUTSIDE that window (a high-BSS
 * static or the C stack), since gapbuf_init overwrites it. */
int editor_main(const char *path, uint16_t start_cursor);

/* After editor_main returns EDITOR_RUN, the absolute ProDOS path of the file
 * to run (the editor's saved-to filename). Valid until the next editor_main
 * call. Lives in editor BSS, NOT the GAPBUF window, so it survives the return. */
const char *editor_saved_path(void);

/* After editor_main returns EDITOR_RUN, the cursor's byte offset in the buffer
 * at the moment Ctrl-R was pressed. The launcher records it in the LASTRUN note
 * and feeds it back as editor_main's `cursor` so the editor reopens where the
 * user left off. Editor BSS, survives the return like editor_saved_path. */
uint16_t editor_saved_cursor(void);

#endif /* SWIFTII_EDITOR_H */
