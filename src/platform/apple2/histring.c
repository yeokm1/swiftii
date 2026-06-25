/* REPL line-history ring — see histring.h and design doc 019.
 *
 * Pure ring + nav logic, lifted verbatim from platform_read_line (including its
 * file-static storage, which keeps cc65's codegen tight) so it can be
 * host-unit-tested. Linked into every build that pulls in the apple2 platform
 * layer (it rides A2_SHARED_SRC), but WITH_LINE_HISTORY collapses it to an
 * empty TU on the II+ REPLs, the Family B Compiler/Runner, and the launcher,
 * so those binaries stay byte-identical. Only the //e REPL interpreters
 * (SWIFTIIE / SWIFTAUX) and the host test compile the real code.
 */
#include "histring.h"

#if defined(WITH_LINE_HISTORY)

#include <string.h>

static char    s_slots[HIST_SLOTS][HIST_LINE]; /* recorded lines (NUL-term)   */
static uint8_t s_count;                         /* valid entries, <= HIST_SLOTS */
static uint8_t s_head;                          /* slot the next line takes     */
static char    s_save[HIST_LINE];               /* in-progress line parked      */

void histring_reset(void) {
  s_count = 0;
  s_head = 0;
}

uint8_t histring_count(void) { return s_count; }

void histring_add(const char *buf, uint16_t len) {
  uint8_t prev;
  if (len == 0 || len >= HIST_LINE) return;
  if (s_count) {
    prev = (uint8_t)((s_head - 1) & HIST_MASK);
    if (s_slots[prev][len] == '\0' && memcmp(s_slots[prev], buf, len) == 0)
      return;
  }
  memcpy(s_slots[s_head], buf, len);
  s_slots[s_head][len] = '\0';
  s_head = (uint8_t)((s_head + 1) & HIST_MASK);
  if (s_count < HIST_SLOTS) ++s_count;
}

const char *histring_older(uint8_t *nav, const char *live, uint16_t live_len) {
  if (*nav >= s_count) return 0; /* already at the oldest entry */
  if (*nav == 0) {               /* park the live line, once, on the first step */
    uint16_t k = (live_len < (HIST_LINE - 1)) ? live_len : (uint16_t)(HIST_LINE - 1);
    memcpy(s_save, live, k);
    s_save[k] = '\0';
  }
  ++*nav;
  return s_slots[(uint8_t)((s_head - *nav) & HIST_MASK)];
}

const char *histring_newer(uint8_t *nav) {
  if (*nav == 0) return 0;             /* already on the live line */
  --*nav;
  if (*nav == 0) return s_save;        /* stepped back onto the parked live line */
  return s_slots[(uint8_t)((s_head - *nav) & HIST_MASK)];
}

#else
/* WITH_LINE_HISTORY off: keep this a non-empty translation unit so the host
 * build's -Werror (ISO C forbids an empty TU) and cc65 both stay happy, while
 * emitting no code or data — the II+ / Family B / launcher binaries that link
 * this object see zero bytes. */
typedef int swiftii_histring_empty_translation_unit;

#endif /* WITH_LINE_HISTORY */
