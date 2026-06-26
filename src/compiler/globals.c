/* Global symbol table.
 *
 * Implementation: a fixed-size array indexed by definition order.
 * Names are stored as zero-padded fixed-width strings (IDENT_MAX bytes,
 * effectively IDENT_MAX-1 chars + an implicit NUL terminator slot).
 *
 * Declaring a name longer than IDENT_MAX-1 chars is a compile-time error.
 * The parser reports it (expect_decl_name in statements.c, with a dedicated
 * "name >11 chars" message); globals_define / locals_declare /
 * funcs_declare also reject it defensively, which guarantees a stored name is
 * always the full source identifier. A *reference* to an over-length name can
 * still be lexed (e.g. a long builtin name, or a typo); names_match clamps the
 * compare so such a lookup stays in bounds and simply misses. See docs/using/LANGUAGE.md
 * section Identifiers for the rationale.
 */
#include "globals.h"

#include <stdint.h>
#include "../common/config.h"
#include "funcs.h"

typedef struct global_entry {
  char name[IDENT_MAX];     /* zero-padded, not NUL-terminated when full */
  unsigned char is_let;
} GlobalEntry;

static GlobalEntry s_entries[MAX_GLOBALS];
static uint8_t s_count;

/* Parallel array for compile-time ctype. Kept out
 * of GlobalEntry to preserve cc65's power-of-2 struct stride — adding
 * a byte to GlobalEntry blows up the array-index multiply path. See
 * feedback_cc65_quirks.md. */
static ctype_t s_ctypes[MAX_GLOBALS];

void globals_reset(void) {
  uint8_t i;
  uint8_t j;
  s_count = 0;
  /* s_ctypes is not re-zeroed here — globals_define overwrites the
   * slot on the next declaration, and stale entries are unreachable
   * via globals_get_ctype's `index >= s_count` guard. */
  for (i = 0; i < MAX_GLOBALS; ++i) {
    for (j = 0; j < IDENT_MAX; ++j) s_entries[i].name[j] = 0;
    s_entries[i].is_let = 0;
  }
  /* The function table and the persistent bytecode arena are
   * conceptually part of the same compile-time state — wiping
   * globals without wiping functions would leave dangling
   * fn_idx → bcbuf_address references. file_runner calls
   * globals_reset() before each fresh compile; REPL's `:reset`
   * meta-command does the same. */
  funcs_reset();
}

uint8_t globals_count(void) {
  return s_count;
}

/* Compare a stored name (zero-padded width IDENT_MAX) to a source span.
 * Equality is on the first min(len, IDENT_MAX-1) bytes; the stored name's
 * remaining bytes (if any) must be zero. The clamp also keeps the read in
 * bounds when `len` exceeds the stored width — e.g. a reference to an
 * over-length name (which can never have been declared, so it misses). */
static int names_match(const char *stored, const char *src, uint16_t len) {
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

int16_t globals_find(const char *name, uint16_t len) {
  uint8_t i;
  if (len == 0) return -1;
  for (i = 0; i < s_count; ++i) {
    if (names_match(s_entries[i].name, name, len)) {
      return (int16_t)i;
    }
  }
  return -1;
}

int16_t globals_define(const char *name, uint16_t len, unsigned char is_let,
                       ctype_t ctype) {
  uint8_t idx;
  uint16_t i;

  if (len == 0) return -1;
  /* Over-length names are a hard error rather than a silent truncation that
   * would collide with another name sharing the IDENT_MAX-1 prefix. This
   * guarantees len <= IDENT_MAX-1 below, so the name stores verbatim. */
  if (len >= (uint16_t)IDENT_MAX) return -1;
  if (globals_find(name, len) >= 0) return -1;
  if (s_count >= MAX_GLOBALS) return -1;

  idx = s_count++;
  for (i = 0; i < len; ++i) s_entries[idx].name[i] = name[i];
  for (i = len; i < (uint16_t)IDENT_MAX; ++i) s_entries[idx].name[i] = 0;
  s_entries[idx].is_let = is_let;
  s_ctypes[idx] = ctype;
  return (int16_t)idx;
}

unsigned char globals_is_let(uint8_t index) {
  if (index >= s_count) return 0;
  return s_entries[index].is_let;
}

ctype_t globals_get_ctype(uint8_t index) {
  if (index >= s_count) return CT_UNKNOWN;
  return s_ctypes[index];
}

void globals_set_ctype(uint8_t index, ctype_t ctype) {
  if (index >= s_count) return;
  s_ctypes[index] = ctype;
}

const char *globals_get_name(uint8_t index) {
  if (index >= s_count) return (const char *)0;
  return s_entries[index].name;
}

void globals_savepoint(uint8_t *count_out) {
  *count_out = s_count;
}

void globals_rollback(uint8_t saved_count) {
  uint8_t i;
  uint8_t j;
  if (saved_count >= s_count) return;
  for (i = saved_count; i < s_count; ++i) {
    for (j = 0; j < IDENT_MAX; ++j) s_entries[i].name[j] = 0;
    s_entries[i].is_let = 0;
  }
  s_count = saved_count;
}
