/* Local-variable symbol table.
 *
 * See locals.h for the contract. The table is reset on every function-
 * body entry; has no nested block scoping, so a single flat
 * scope per function suffices.
 */
#include "locals.h"

#include <stdint.h>
#include "../common/config.h"

typedef struct local_entry {
  char          name[IDENT_MAX]; /* zero-padded, not NUL-terminated when full */
  unsigned char is_let;
} LocalEntry;

static LocalEntry s_entries[MAX_LOCALS];
static uint8_t    s_count;

/* Parallel ctype array — see globals.c for the cc65 stride rationale. */
static ctype_t s_ctypes[MAX_LOCALS];

void locals_reset(void) {
  uint8_t i;
  uint8_t j;
  /* s_ctypes is not re-zeroed — see globals.c. */
  for (i = 0; i < MAX_LOCALS; ++i) {
    for (j = 0; j < (uint8_t)IDENT_MAX; ++j) s_entries[i].name[j] = 0;
    s_entries[i].is_let = 0;
  }
  s_count = 0;
}

uint8_t locals_count(void) {
  return s_count;
}

/* Same compare convention as globals.c / funcs.c — see globals.c for the
 * clamp rationale (declared names are <= IDENT_MAX-1; a longer reference is
 * clamped to stay in bounds and then misses). */
static int name_equals(const char *stored, const char *src, uint16_t len) {
  uint16_t cmp_len;
  uint16_t i;
  cmp_len = (len < (uint16_t)(IDENT_MAX - 1)) ? len : (uint16_t)(IDENT_MAX - 1);
  for (i = 0; i < cmp_len; ++i) {
    if (stored[i] != src[i]) return 0;
  }
  if (cmp_len < (uint16_t)(IDENT_MAX - 1)) {
    if (stored[cmp_len] != 0) return 0;
  }
  return 1;
}

int16_t locals_find(const char *name, uint16_t len) {
  uint8_t i;
  if (len == 0) return -1;
  for (i = 0; i < s_count; ++i) {
    if (name_equals(s_entries[i].name, name, len)) return (int16_t)i;
  }
  return -1;
}

int16_t locals_declare(const char *name, uint16_t len, unsigned char is_let,
                       ctype_t ctype) {
  uint8_t idx;
  uint16_t i;

  /* Over-length names are a hard error, not a silent truncation/collision;
   * this guarantees len <= IDENT_MAX-1 below, so the name stores verbatim.
   * (len == 0 is the deliberate anonymous-slot placeholder — accepted.) */
  if (len >= (uint16_t)IDENT_MAX) return -1;
  if (locals_find(name, len) >= 0) return -1;
  if (s_count >= MAX_LOCALS) return -1;

  idx = s_count;
  for (i = 0; i < len; ++i) s_entries[idx].name[i] = name[i];
  for (i = len; i < (uint16_t)IDENT_MAX; ++i) s_entries[idx].name[i] = 0;
  s_entries[idx].is_let = is_let;
  s_ctypes[idx] = ctype;
  ++s_count;
  return (int16_t)idx;
}

void locals_truncate(uint8_t n) {
  /* Only ever shrinks. Entries above `n` keep their stale bytes but are
   * no longer visible to locals_find/_count, and locals_declare fully
   * overwrites a slot's name before reuse, so no scrub is needed. */
  if (n < s_count) s_count = n;
}

unsigned char locals_is_let(uint8_t index) {
  if (index >= s_count) return 0;
  return s_entries[index].is_let;
}

ctype_t locals_get_ctype(uint8_t index) {
  if (index >= s_count) return CT_UNKNOWN;
  return s_ctypes[index];
}

void locals_set_ctype(uint8_t index, ctype_t ctype) {
  if (index >= s_count) return;
  s_ctypes[index] = ctype;
}
