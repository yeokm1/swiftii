/* Editor file I/O — load a .swift into the gap buffer, save it back out.
 *
 * The editor is a MAIN-only binary (it does not map the language card), so
 * ProDOS's MLI is resident and ordinary file I/O works — unlike the
 * interpreters, which dropped on-target MLI. Load streams the
 * file into the gap buffer a chunk at a time (no second full-size buffer:
 * main RAM only has room for the one ~24 KB gap buffer); save streams it
 * back a line at a time.
 *
 * Canonicalisation: on a //+ build a `.swift` file's gap buffer holds the raw
 * typed bytes (uppercase, literal `<%`), so save runs each line through
 * `input_translate` (design doc 003) to write canonical lowercase Swift to
 * disk — matching the REPL's "translate on Return" model — and load runs the
 * inverse so capitals survive the round trip. The //e launcher build
 * (LITE_IIE) already holds canonical bytes, so it loads/saves verbatim.
 *
 * That whole Swift typing model (case markers + auto-lowercase + the
 * `<% %> <: :> ??/ ??!` digraphs, on input AND in the display expansion) is
 * Swift-only. A plain text file (a non-.swift name such as README.TXT) is
 * loaded, displayed, and saved VERBATIM on every build, so an all-caps help
 * file reads natively on a //+ and round-trips byte-for-byte. See
 * editor_path_is_swift.
 *
 * The low-level open/read/write/close use prodos.c's raw MLI wrapper on the
 * Apple II and stdio/POSIX on the host, so the load/save round trip is
 * host-testable.
 */
#ifndef SWIFTII_EDITOR_FILEIO_H
#define SWIFTII_EDITOR_FILEIO_H

#include "gapbuf.h"

typedef enum editor_file_result {
  EFIO_OK = 0,
  EFIO_NOTFOUND,  /* open-for-read failed (no such file) */
  EFIO_TOOBIG,    /* file larger than the gap buffer capacity */
  EFIO_IOERR      /* read/write/create error */
} EditorFileResult;

/// Replace the gap buffer contents with the file at `path`, cursor at the
/// top. Streams in; the buffer is left empty on EFIO_TOOBIG. `cooked` selects
/// the //+ digraph typing model: non-zero rewrites each line canonical -> input
/// form (input_untranslate) so capitals/digraphs round-trip through a re-save;
/// zero loads the bytes verbatim ("raw" mode, and every non-.swift file). The
/// flag is ignored on a //e build (always verbatim). The caller (editor.c)
/// derives the default from the extension and lets the user toggle it.
EditorFileResult editor_file_load(GapBuf *gb, const char *path, int cooked);

/// Write the gap buffer to `path` (created/truncated). When `cooked` is set,
/// each line is canonicalised via input_translate (//+ digraph model); when it
/// is zero the bytes are written verbatim (raw mode / non-.swift files). The
/// flag is ignored on a //e build (always verbatim). Pair it with the same
/// `cooked` value the matching load used so the round trip is stable.
EditorFileResult editor_file_save(const GapBuf *gb, const char *path,
                                  int cooked);

/// Non-zero iff `path` names a Swift source (ends in ".swift", case-insensitive)
/// or is empty (untitled scratch defaults to Swift). Gates the editor's
/// Swift-only digraph entry + display expansion (see this header's preamble).
int editor_path_is_swift(const char *path);

#endif /* SWIFTII_EDITOR_FILEIO_H */
