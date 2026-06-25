/* Text navigation over a gap buffer — see textnav.h.
 *
 * These scans run on the editor's hot path (cursor placement and viewport
 * scroll happen every keystroke), so the byte reads are inlined against
 * gb->buf with the gap split rather than going through gapbuf_at — a
 * function call per byte was a measurable share of the per-keystroke cost on
 * the 1 MHz target. The work is bounded by the cursor position / line
 * length, not the whole buffer. */
#include "textnav.h"

/* Read the logical byte at `i`, inline (caller guarantees i < len). `buf`,
 * `gs` (gap_start) and `ge` (gap_end) are cached locals in each function. */
#define TN_BYTE(buf, gs, ge, i) \
  ((i) < (gs) ? (buf)[i] : (buf)[(uint16_t)((ge) + ((i) - (gs)))])

static uint16_t clamp_pos(const GapBuf *gb, uint16_t pos) {
  uint16_t len = gapbuf_len(gb);
  return pos > len ? len : pos;
}

uint16_t textnav_line_start(const GapBuf *gb, uint16_t pos) {
  const uint8_t *buf = gb->buf;
  uint16_t gs = gb->gap_start;
  uint16_t ge = gb->gap_end;
  uint16_t i;
  pos = clamp_pos(gb, pos);
  i = pos;
  while (i > 0 && TN_BYTE(buf, gs, ge, (uint16_t)(i - 1)) != '\n') --i;
  return i;
}

uint16_t textnav_line_end(const GapBuf *gb, uint16_t pos) {
  const uint8_t *buf = gb->buf;
  uint16_t gs = gb->gap_start;
  uint16_t ge = gb->gap_end;
  uint16_t len = gapbuf_len(gb);
  uint16_t i;
  pos = pos > len ? len : pos;
  i = pos;
  while (i < len && TN_BYTE(buf, gs, ge, i) != '\n') ++i;
  return i;
}

uint16_t textnav_col(const GapBuf *gb, uint16_t pos) {
  pos = clamp_pos(gb, pos);
  return (uint16_t)(pos - textnav_line_start(gb, pos));
}

uint16_t textnav_line_index(const GapBuf *gb, uint16_t pos) {
  const uint8_t *buf = gb->buf;
  uint16_t gs = gb->gap_start;
  uint16_t ge = gb->gap_end;
  uint16_t i;
  uint16_t line;
  pos = clamp_pos(gb, pos);
  line = 0;
  for (i = 0; i < pos; ++i) {
    if (TN_BYTE(buf, gs, ge, i) == '\n') ++line;
  }
  return line;
}

uint16_t textnav_line_count(const GapBuf *gb) {
  const uint8_t *buf = gb->buf;
  uint16_t gs = gb->gap_start;
  uint16_t ge = gb->gap_end;
  uint16_t len = gapbuf_len(gb);
  uint16_t i;
  uint16_t lines;
  lines = 1;
  for (i = 0; i < len; ++i) {
    if (TN_BYTE(buf, gs, ge, i) == '\n') ++lines;
  }
  return lines;
}

uint16_t textnav_line_at(const GapBuf *gb, uint16_t line) {
  const uint8_t *buf = gb->buf;
  uint16_t gs = gb->gap_start;
  uint16_t ge = gb->gap_end;
  uint16_t len = gapbuf_len(gb);
  uint16_t i;
  uint16_t seen;
  if (line == 0) return 0;
  seen = 0;
  for (i = 0; i < len; ++i) {
    if (TN_BYTE(buf, gs, ge, i) == '\n') {
      ++seen;
      if (seen == line) return (uint16_t)(i + 1);
    }
  }
  /* Past the last line: report the start of the last line. */
  return textnav_line_start(gb, len);
}

/* Move to `target_col` within the line that starts at `line_start`,
 * clamped to that line's end. */
static uint16_t pos_in_line(const GapBuf *gb, uint16_t line_start,
                            uint16_t target_col) {
  uint16_t line_end;
  uint16_t line_len;
  line_end = textnav_line_end(gb, line_start);
  line_len = (uint16_t)(line_end - line_start);
  if (target_col > line_len) target_col = line_len;
  return (uint16_t)(line_start + target_col);
}

uint16_t textnav_up(const GapBuf *gb, uint16_t pos) {
  uint16_t start;
  uint16_t col;
  uint16_t prev_start;
  pos = clamp_pos(gb, pos);
  start = textnav_line_start(gb, pos);
  if (start == 0) return pos; /* already on the first line */
  col = (uint16_t)(pos - start);
  prev_start = textnav_line_start(gb, (uint16_t)(start - 1));
  return pos_in_line(gb, prev_start, col);
}

uint16_t textnav_down(const GapBuf *gb, uint16_t pos) {
  uint16_t len;
  uint16_t start;
  uint16_t col;
  uint16_t end;
  len = gapbuf_len(gb);
  pos = pos > len ? len : pos;
  start = textnav_line_start(gb, pos);
  col = (uint16_t)(pos - start);
  end = textnav_line_end(gb, pos);
  if (end >= len) return pos; /* last line has no '\n' after it */
  return pos_in_line(gb, (uint16_t)(end + 1), col);
}
