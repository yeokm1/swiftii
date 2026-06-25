/* REPL line-history ring unit tests (host).
 *
 * Exercises histring_add / histring_older / histring_newer — the //e up/down
 * recall logic — with no keyboard or screen. The nav index and the
 * (head - nav) & MASK ring walk are the off-by-one-prone spots the //e
 * binaries could never regression-test on target (design doc 019); this pins
 * them: record/de-dup, older/newer stepping, the in-progress park/restore,
 * nav saturation at both ends, and ring wrap + eviction past HIST_SLOTS.
 *
 * The ring is file-static, so each test calls histring_reset() first and reads
 * the size back via histring_count().
 *
 * Built with -DWITH_LINE_HISTORY (Makefile target-specific flag) so the gated
 * histring.c body is live on the host.
 */
#include <stdint.h>
#include <string.h>

#include "platform/apple2/histring.h"

#define EXPECT(cond, code) do { if (!(cond)) return (code); } while (0)

/* NULL-safe string compare: both NULL ok, else strcmp == 0. */
static int streq(const char *a, const char *b) {
  if (a == 0 || b == 0) return a == b;
  return strcmp(a, b) == 0;
}

static void add(const char *s) {
  histring_add(s, (uint16_t)strlen(s));
}

/* Record three lines, then walk older oldest-ward and confirm we stop. */
int test_histring_older_basic(void) {
  uint8_t nav = 0;
  histring_reset();
  add("one");
  add("two");
  add("three");
  EXPECT(histring_count() == 3, 1);

  EXPECT(streq(histring_older(&nav, "", 0), "three"), 2);
  EXPECT(nav == 1, 3);
  EXPECT(streq(histring_older(&nav, "", 0), "two"), 4);
  EXPECT(streq(histring_older(&nav, "", 0), "one"), 5);
  EXPECT(nav == 3, 6);
  /* At the oldest entry: no move, nav unchanged. */
  EXPECT(histring_older(&nav, "", 0) == 0, 7);
  EXPECT(nav == 3, 8);
  return 0;
}

/* Walk older then newer; stepping below the newest returns NULL (live line). */
int test_histring_newer_walks_back(void) {
  uint8_t nav = 0;
  histring_reset();
  add("a");
  add("b");
  add("c");

  histring_older(&nav, "", 0); /* -> "c", nav 1 */
  histring_older(&nav, "", 0); /* -> "b", nav 2 */
  histring_older(&nav, "", 0); /* -> "a", nav 3 */
  EXPECT(nav == 3, 1);

  EXPECT(streq(histring_newer(&nav), "b"), 2);
  EXPECT(nav == 2, 3);
  EXPECT(streq(histring_newer(&nav), "c"), 4);
  EXPECT(nav == 1, 5);
  /* Back onto the live slot: parked line (here empty), nav 0. */
  EXPECT(streq(histring_newer(&nav), ""), 6);
  EXPECT(nav == 0, 7);
  /* Already on the live line: no move. */
  EXPECT(histring_newer(&nav) == 0, 8);
  EXPECT(nav == 0, 9);
  return 0;
}

/* The in-progress line is parked (first step off live) and restored on the way
 * back down. */
int test_histring_parks_in_progress(void) {
  uint8_t nav = 0;
  histring_reset();
  add("old");

  /* User has typed "draft" then presses up. */
  EXPECT(streq(histring_older(&nav, "draft", 5), "old"), 1);
  EXPECT(nav == 1, 2);
  /* Down again restores exactly what was typed. */
  EXPECT(streq(histring_newer(&nav), "draft"), 3);
  EXPECT(nav == 0, 4);
  return 0;
}

/* Park happens only on the 0->1 transition, so a deeper recall doesn't
 * overwrite the saved live line with a history entry. */
int test_histring_park_only_once(void) {
  uint8_t nav = 0;
  histring_reset();
  add("x");
  add("y");

  histring_older(&nav, "LIVE", 4); /* parks "LIVE", -> "y" */
  histring_older(&nav, "y", 1);    /* nav already 1: must NOT re-park */
  /* Walk all the way back: the live line is still the original "LIVE". */
  histring_newer(&nav);            /* -> "y" */
  EXPECT(streq(histring_newer(&nav), "LIVE"), 1);
  EXPECT(nav == 0, 2);
  return 0;
}

/* Empty lines, over-long lines, and an exact repeat of the latest are not
 * recorded. */
int test_histring_add_rejects(void) {
  char big[HIST_LINE + 8];
  histring_reset();

  histring_add("", 0);
  EXPECT(histring_count() == 0, 1);

  memset(big, 'z', sizeof big);
  histring_add(big, HIST_LINE);      /* len == HIST_LINE: too long */
  EXPECT(histring_count() == 0, 2);
  histring_add(big, HIST_LINE - 1);  /* len == HIST_LINE-1: fits */
  EXPECT(histring_count() == 1, 3);

  add("dup");
  add("dup");                         /* exact repeat of latest: dropped */
  EXPECT(histring_count() == 2, 4);
  add("dup2");
  add("dup");                         /* not the latest now: recorded */
  EXPECT(histring_count() == 4, 5);
  return 0;
}

/* More than HIST_SLOTS lines: count caps, the oldest entries are evicted, and
 * the (head - nav) & MASK walk still returns the right slots across the wrap. */
int test_histring_wraps_and_evicts(void) {
  uint8_t nav = 0;
  char line[8];
  int k;
  histring_reset();

  /* Record l0..l9 (10 lines) into 8 slots: l0,l1 evicted, l2 is oldest. */
  for (k = 0; k < 10; ++k) {
    line[0] = 'l';
    line[1] = (char)('0' + k);
    line[2] = '\0';
    add(line);
  }
  EXPECT(histring_count() == HIST_SLOTS, 1);

  /* Older walks newest (l9) down to oldest (l2), exactly HIST_SLOTS steps. */
  EXPECT(streq(histring_older(&nav, "", 0), "l9"), 2);
  for (k = 8; k >= 2; --k) {
    line[0] = 'l';
    line[1] = (char)('0' + k);
    line[2] = '\0';
    EXPECT(streq(histring_older(&nav, "", 0), line), 3);
  }
  EXPECT(nav == HIST_SLOTS, 4);
  /* Can't go past the oldest live slot. */
  EXPECT(histring_older(&nav, "", 0) == 0, 5);
  return 0;
}
