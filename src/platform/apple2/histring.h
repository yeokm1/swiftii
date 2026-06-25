/* REPL line-history ring — the pure decision half of the //e up/down recall.
 *
 * platform_read_line (keyboard.c) does the keyboard + screen I/O; this module
 * owns the recall *logic*: the ring of recent lines, the de-dup on record, and
 * the nav index that walks older/newer while parking the in-progress line. None
 * of it touches the keyboard, screen, or ProDOS, so it is host-unit-tested
 * directly (tests/platform/histring_test.c) — the nav/ring index is the one
 * off-by-one-prone spot the //e binaries could never regression-test before.
 *
 * The ring is a single file-static instance inside histring.c (exactly as it
 * was when this lived in keyboard.c): cc65 emits far tighter code for absolute
 * access to file statics than for indexing a caller-passed struct pointer, so
 * keeping it global holds the //e binaries near byte-neutral. The hidden state
 * is fine for testing — histring_reset() clears it between cases and
 * histring_count() reads it back. Only `nav` (the per-line step-back index) is
 * the caller's, because each input line starts a fresh walk.
 *
 * Gating: this is a //e REPL-interpreter convenience (WITH_IIE && !WITH_SWB —
 * the //e keyboard has the up/down keys and those binaries have the BSS room).
 * The II+ REPLs (no WITH_IIE) and the Family B Compiler/Runner (WITH_SWB)
 * derive WITH_LINE_HISTORY = off, so on those builds this whole header/TU
 * collapses to nothing and the binaries stay byte-identical. The host test
 * defines WITH_LINE_HISTORY directly to exercise the logic. See design doc 019.
 *
 * Pure C90; compiles on host and target.
 */
#ifndef SWIFTII_HISTRING_H
#define SWIFTII_HISTRING_H

/* Derive the //e-only gate unless a build (the host test) forced it on. */
#if !defined(WITH_LINE_HISTORY)
#  if defined(WITH_IIE) && !defined(WITH_SWB)
#    define WITH_LINE_HISTORY 1
#  endif
#endif

#if defined(WITH_LINE_HISTORY)

#include <stdint.h>

/* HIST_SLOTS is a power of two so the head/recall index wraps with a mask
 * instead of a divide. A line longer than HIST_LINE-1 simply isn't recorded,
 * so a recalled line is always an exact copy of what was entered. 8 * 128 plus
 * the one-line park scratch is ~1.1 KB of BSS (the //e binaries have room). */
#define HIST_SLOTS 8
#define HIST_MASK  (HIST_SLOTS - 1)
#define HIST_LINE  128

/// Clear the ring (count + head). The interpreter never calls this — history
/// accumulates for the whole session — but the host tests reset between cases.
void histring_reset(void);

/// Number of recorded lines (0..HIST_SLOTS). For tests / introspection.
uint8_t histring_count(void);

/// Record a completed input line. No-ops on an empty line, a line that doesn't
/// fit HIST_LINE, or an exact repeat of the most recent entry (the common
/// "run it again" case).
void histring_add(const char *buf, uint16_t len);

/// Step to an OLDER line (up-arrow / Ctrl-P). `*nav` is the caller's step-back
/// index: 0 = the live line being typed, 1 = newest history entry, up to
/// count = oldest. On the first step off the live line it parks `live`/
/// `live_len` so histring_newer can bring it back. Advances `*nav` and returns
/// the line to show, or NULL when already at the oldest (no move).
const char *histring_older(uint8_t *nav, const char *live, uint16_t live_len);

/// Step to a NEWER line (down-arrow / Ctrl-N). Retreats `*nav` and returns the
/// line to show — the parked in-progress line when stepping back onto the live
/// slot — or NULL when already on the live line (no move).
const char *histring_newer(uint8_t *nav);

#endif /* WITH_LINE_HISTORY */
#endif /* SWIFTII_HISTRING_H */
