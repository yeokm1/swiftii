/* Gap buffer — the editor's in-RAM text model (design doc 006).
 *
 * A gap buffer is a contiguous byte array with a movable hole (the
 * "gap") sitting at the cursor. Insertions and deletions at the cursor
 * are O(1) — they only grow or shrink the gap — and cursor moves are
 * O(distance), copying the crossed bytes from one side of the gap to the
 * other. For human-rate editing on a 1 MHz 6502 this is plenty fast: the
 * common case (type, backspace, occasionally jump a line) touches a
 * handful of bytes.
 *
 * Storage is caller-provided (init takes a buffer + capacity) so the
 * Apple II build can point it at a large BSS array while host tests use a
 * small stack buffer. The struct holds no allocation of its own.
 *
 * This module is pure C90 with no platform dependencies: it compiles
 * under both cc65 (target) and clang (host unit tests).
 */
#ifndef SWIFTII_EDITOR_GAPBUF_H
#define SWIFTII_EDITOR_GAPBUF_H

#include <stdint.h>

/* Layout invariant at all times:
 *   0 <= gap_start <= gap_end <= cap
 *   bytes [0, gap_start)      = text before the cursor
 *   bytes [gap_start, gap_end)= the gap (cursor sits at its left edge)
 *   bytes [gap_end, cap)      = text after the cursor
 * Logical length = gap_start + (cap - gap_end); cursor position = gap_start. */
typedef struct gap_buf {
  uint8_t *buf;
  uint16_t cap;
  uint16_t gap_start;
  uint16_t gap_end;
} GapBuf;

/// Bind `gb` to caller storage `storage` of `cap` bytes and empty it
/// (whole buffer is gap, cursor at 0).
void gapbuf_init(GapBuf *gb, uint8_t *storage, uint16_t cap);

/// Logical text length (bytes), excluding the gap.
uint16_t gapbuf_len(const GapBuf *gb);

/// Cursor position as a logical offset (0..len).
uint16_t gapbuf_pos(const GapBuf *gb);

/// Insert one byte at the cursor, advancing it. Returns 1 on success, 0
/// if the buffer is full (gap exhausted).
int gapbuf_insert(GapBuf *gb, uint8_t ch);

/// Delete the byte left of the cursor (backspace). Returns 1, or 0 if the
/// cursor is already at the start.
int gapbuf_delete_left(GapBuf *gb);

/// Delete the byte right of the cursor (forward delete). Returns 1, or 0
/// if the cursor is already at the end.
int gapbuf_delete_right(GapBuf *gb);

/// Move the cursor left by up to `n` bytes (clamped at the start).
void gapbuf_move_left(GapBuf *gb, uint16_t n);

/// Move the cursor right by up to `n` bytes (clamped at the end).
void gapbuf_move_right(GapBuf *gb, uint16_t n);

/// Move the cursor to logical offset `pos` (clamped to 0..len).
void gapbuf_move_to(GapBuf *gb, uint16_t pos);

/// Logical byte at offset `pos` (0..len-1). Returns 0 if out of range.
uint8_t gapbuf_at(const GapBuf *gb, uint16_t pos);

/// Copy the logical text into `out` (up to `cap` bytes) for saving.
/// Returns the number of bytes written (min(len, cap)).
uint16_t gapbuf_serialize(const GapBuf *gb, uint8_t *out, uint16_t cap);

/// Replace the contents with `len` bytes from `src`, cursor at 0 (for
/// loading a file). Returns 1 on success, 0 if `len` exceeds capacity.
int gapbuf_load(GapBuf *gb, const uint8_t *src, uint16_t len);

#endif /* SWIFTII_EDITOR_GAPBUF_H */
