/* Text navigation — pure logical-line queries over a gap buffer.
 *
 * The gap buffer (gapbuf.h) stores bytes; the editor thinks in lines. This
 * module answers the line questions the screen renderer and the cursor
 * commands (Ctrl-A/E start/end of line, Ctrl-O/L up/down) need: where a
 * line starts and ends, which line a position is on, and the position one
 * line up/down at the same column.
 *
 * "Line" means a *logical* line — a run of bytes delimited by '\n'. The
 * trailing newline is not part of the line it ends. Columns and positions
 * here are logical byte offsets, independent of how a byte is *displayed*
 * (a '{' is one logical column even though it draws as "<%" on a pre-IIe
 * screen — that display-width mapping is the renderer's job, not this
 * module's). Everything is read-only: nothing here mutates the buffer.
 *
 * Pure C90, no platform dependencies — compiles on host and target.
 */
#ifndef SWIFTII_EDITOR_TEXTNAV_H
#define SWIFTII_EDITOR_TEXTNAV_H

#include <stdint.h>

#include "gapbuf.h"

/// Position of the first byte of the line containing `pos` (just after the
/// preceding '\n', or 0). `pos` is clamped to 0..len.
uint16_t textnav_line_start(const GapBuf *gb, uint16_t pos);

/// Position of the '\n' ending the line containing `pos`, or the buffer
/// length if the line is the last and unterminated. `pos` clamped to 0..len.
uint16_t textnav_line_end(const GapBuf *gb, uint16_t pos);

/// Column of `pos` within its line (pos - line_start), a logical byte count.
uint16_t textnav_col(const GapBuf *gb, uint16_t pos);

/// 0-based index of the line containing `pos`.
uint16_t textnav_line_index(const GapBuf *gb, uint16_t pos);

/// Total number of logical lines (always >= 1; an empty buffer is 1 line).
uint16_t textnav_line_count(const GapBuf *gb);

/// Start position of the 0-based line `line`. Clamps: a `line` past the end
/// returns the start of the last line.
uint16_t textnav_line_at(const GapBuf *gb, uint16_t line);

/// Position one line above `pos`, keeping the same column where the upper
/// line is long enough (else its end). Returns `pos` unchanged on line 0.
uint16_t textnav_up(const GapBuf *gb, uint16_t pos);

/// Position one line below `pos`, keeping the same column where the lower
/// line is long enough (else its end). Returns `pos` unchanged on the last
/// line.
uint16_t textnav_down(const GapBuf *gb, uint16_t pos);

#endif /* SWIFTII_EDITOR_TEXTNAV_H */
