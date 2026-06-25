/* Function symbol table.
 *
 * See funcs.h for the contract. Entries are stored field-by-field
 * (no struct-by-value) because cc65 warns and bloats on struct
 * arguments — see `feedback_cc65_quirks.md`.
 */
#include "funcs.h"

#include <stdint.h>
#include "../common/config.h"
#include "bcbuf.h"

typedef struct fn_entry {
  char          name[IDENT_MAX]; /* zero-padded, not NUL-terminated when full */
  uint16_t      bc_start;        /* absolute bcbuf offset of body */
  uint8_t       param_count;
  unsigned char has_return;      /* 1 if declared `-> SomeType`, 0 otherwise */
} FnEntry;

static FnEntry s_entries[MAX_FUNCS];
static uint8_t s_count;

/* Parallel ctype storage. Held outside FnEntry so
 * the struct stride stays at 16 bytes (power of 2) — adding ctypes
 * inline would push FnEntry to 23 B and bloat cc65's array-index
 * multiply at every read site. See feedback_cc65_quirks.md. */
static ctype_t s_ret_ctypes[MAX_FUNCS];
static ctype_t s_param_ctypes[MAX_FUNCS][FUNC_MAX_TRACKED_PARAMS];

void funcs_reset(void) {
  uint8_t i;
  uint8_t j;
  /* s_ret_ctypes / s_param_ctypes are not re-zeroed; funcs_declare and
   * funcs_set_signature overwrite per-slot on the next declaration,
   * and stale entries are unreachable via the index >= s_count guard
   * on each accessor. */
  for (i = 0; i < MAX_FUNCS; ++i) {
    for (j = 0; j < (uint8_t)IDENT_MAX; ++j) s_entries[i].name[j] = 0;
    s_entries[i].bc_start = 0;
    s_entries[i].param_count = 0;
    s_entries[i].has_return = 0;
  }
  s_count = 0;
  bcbuf_arena_reset();
}

uint8_t funcs_count(void) {
  return s_count;
}

/* Compare a stored name (zero-padded width IDENT_MAX) to a source span.
 * Mirrors the globals.c convention — see there for the clamp rationale
 * (declared names are <= IDENT_MAX-1; a longer reference is clamped to
 * stay in bounds and then misses). */
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

int16_t funcs_find(const char *name, uint16_t len) {
  uint8_t i;
  if (len == 0) return -1;
  for (i = 0; i < s_count; ++i) {
    if (name_equals(s_entries[i].name, name, len)) return (int16_t)i;
  }
  return -1;
}

int16_t funcs_declare(const char *name, uint16_t len) {
  uint8_t idx;
  uint16_t i;

  if (len >= (uint16_t)IDENT_MAX) return -1;       /* over-length: hard error */
  if (funcs_find(name, len) >= 0) return -1;       /* duplicate */
  if (s_count >= MAX_FUNCS) return -1;             /* table full */

  /* The reject above guarantees len <= IDENT_MAX-1, so store verbatim. */
  idx = s_count;
  for (i = 0; i < len; ++i) s_entries[idx].name[i] = name[i];
  for (i = len; i < (uint16_t)IDENT_MAX; ++i) s_entries[idx].name[i] = 0;
  s_entries[idx].bc_start = 0;
  s_entries[idx].param_count = 0;
  s_entries[idx].has_return = 0;
  /* s_ret_ctypes[idx] and s_param_ctypes[idx][*] are set by
   * funcs_set_signature / funcs_set_param_ctype during the same
   * parse_func_decl call before any reads. */
  ++s_count;
  return (int16_t)idx;
}

#ifdef WITH_SWB
/* `.swb` runner load — Family B (Compiler/Runner) only, gated out of the
 * Family A interpreters at the MAIN ceiling (see heap.c's WITH_SWB block). */
int16_t funcs_add_runtime(uint16_t bc_start, uint8_t param_count,
                          unsigned char has_return) {
  uint8_t idx;
  if (s_count >= MAX_FUNCS) return -1;
  idx = s_count;
  /* Runtime-only: name + ctypes are never read by the VM (OP_CALL uses
   * bc_start + param_count), so leave the name zeroed and ret ctype VOID. */
  s_entries[idx].name[0] = 0;
  s_entries[idx].bc_start = bc_start;
  s_entries[idx].param_count = param_count;
  s_entries[idx].has_return = has_return;
  s_ret_ctypes[idx] = (ctype_t)CT_VOID;
  ++s_count;
  return (int16_t)idx;
}
#endif /* WITH_SWB */

void funcs_set_signature(uint8_t fn_idx, uint8_t param_count,
                         unsigned char has_return, ctype_t ret_ctype) {
  if (fn_idx >= s_count) return;
  s_entries[fn_idx].param_count = param_count;
  s_entries[fn_idx].has_return = has_return;
  s_ret_ctypes[fn_idx] = has_return ? ret_ctype : (ctype_t)CT_VOID;
}

void funcs_set_param_ctype(uint8_t fn_idx, uint8_t param_idx, ctype_t ctype) {
  if (fn_idx >= s_count) return;
  if (param_idx >= (uint8_t)FUNC_MAX_TRACKED_PARAMS) return;
  s_param_ctypes[fn_idx][param_idx] = ctype;
}

ctype_t funcs_get_param_ctype(uint8_t fn_idx, uint8_t param_idx) {
  if (fn_idx >= s_count) return CT_UNKNOWN;
  if (param_idx >= (uint8_t)FUNC_MAX_TRACKED_PARAMS) return CT_UNKNOWN;
  return s_param_ctypes[fn_idx][param_idx];
}

ctype_t funcs_get_ret_ctype(uint8_t fn_idx) {
  if (fn_idx >= s_count) return CT_VOID;
  return s_ret_ctypes[fn_idx];
}

void funcs_set_start(uint8_t fn_idx, uint16_t bc_start) {
  if (fn_idx >= s_count) return;
  s_entries[fn_idx].bc_start = bc_start;
}

uint16_t funcs_get_start(uint8_t fn_idx) {
  if (fn_idx >= s_count) return (uint16_t)0xFFFF;
  return s_entries[fn_idx].bc_start;
}

uint8_t funcs_get_param_count(uint8_t fn_idx) {
  if (fn_idx >= s_count) return (uint8_t)0xFF;
  return s_entries[fn_idx].param_count;
}

unsigned char funcs_has_return(uint8_t fn_idx) {
  if (fn_idx >= s_count) return 0;
  return s_entries[fn_idx].has_return;
}

#if SWIFTII_FUNC_REDEF
/* REPL function redefinition: rebinding an existing name reuses its
 * table slot and repoints it at the freshly compiled body (the old body is
 * left in the arena as dead space, reclaimed by funcs_reset/:reset). To keep a
 * compile atomic we snapshot the slot's mutable fields before they are
 * overwritten, so funcs_rollback can restore the prior definition if the new
 * body fails to compile. A single slot is tracked: one redefinition per
 * compiled line is the realistic case, and the snapshot holds the most recent.
 * The marker is cleared at funcs_savepoint (start of every compile) and after a
 * rollback restore. */
static int16_t      s_replace_idx = -1;   /* slot being rebound, or -1 = none */
static uint16_t     s_replace_bc_start;
static uint8_t      s_replace_param_count;
static unsigned char s_replace_has_return;
static ctype_t      s_replace_ret_ctype;

void funcs_begin_replace(uint8_t fn_idx) {
  if (fn_idx >= s_count) return;
  s_replace_idx         = (int16_t)fn_idx;
  s_replace_bc_start    = s_entries[fn_idx].bc_start;
  s_replace_param_count = s_entries[fn_idx].param_count;
  s_replace_has_return  = s_entries[fn_idx].has_return;
  s_replace_ret_ctype   = s_ret_ctypes[fn_idx];
}
#endif

void funcs_savepoint(uint8_t *count_out) {
  *count_out = s_count;
#if SWIFTII_FUNC_REDEF
  s_replace_idx = -1;
#endif
}

void funcs_rollback(uint8_t saved_count) {
  uint8_t i;
  uint8_t j;
  if (saved_count > s_count) return;
  for (i = saved_count; i < s_count; ++i) {
    for (j = 0; j < (uint8_t)IDENT_MAX; ++j) s_entries[i].name[j] = 0;
    s_entries[i].bc_start = 0;
    s_entries[i].param_count = 0;
    s_entries[i].has_return = 0;
  }
  s_count = saved_count;
#if SWIFTII_FUNC_REDEF
  /* If this compile had rebound an existing slot (index < saved_count, so it
   * survives the truncation above), restore its prior definition. */
  if (s_replace_idx >= 0 && (uint8_t)s_replace_idx < s_count) {
    uint8_t k = (uint8_t)s_replace_idx;
    s_entries[k].bc_start    = s_replace_bc_start;
    s_entries[k].param_count = s_replace_param_count;
    s_entries[k].has_return  = s_replace_has_return;
    s_ret_ctypes[k]          = s_replace_ret_ctype;
  }
  s_replace_idx = -1;
#endif
}
