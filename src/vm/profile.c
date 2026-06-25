/* Per-opcode dispatch counter — host-only profile build.
 *
 * Compiled into the bench binary at build/host/swiftii_bench when the
 * Makefile defines SWIFTII_PROFILE. The regular host and cc65 builds
 * see this file as an empty translation unit.
 */
#include "profile.h"

#ifdef SWIFTII_PROFILE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "opcodes.h"

static unsigned long s_counts[256];
static unsigned long s_total;
static int           s_atexit_armed;

static const char *opname(unsigned char op) {
  switch (op) {
    case OP_NIL:           return "OP_NIL";
    case OP_TRUE:          return "OP_TRUE";
    case OP_FALSE:         return "OP_FALSE";
    case OP_INT_U8:        return "OP_INT_U8";
    case OP_INT_I16:       return "OP_INT_I16";
    case OP_CONST:         return "OP_CONST";
    case OP_STR:           return "OP_STR";
    case OP_OPT_SOME:      return "OP_OPT_SOME";
    case OP_OPT_NONE:      return "OP_OPT_NONE";
    case OP_POP:           return "OP_POP";
    case OP_DUP:           return "OP_DUP";
    case OP_SWAP:          return "OP_SWAP";
    case OP_OVER:          return "OP_OVER";
    case OP_POP_N:         return "OP_POP_N";
    case OP_GET_GLOBAL:    return "OP_GET_GLOBAL";
    case OP_SET_GLOBAL:    return "OP_SET_GLOBAL";
    case OP_DEFINE_GLOBAL: return "OP_DEFINE_GLOBAL";
    case OP_GET_LOCAL:     return "OP_GET_LOCAL";
    case OP_SET_LOCAL:     return "OP_SET_LOCAL";
    case OP_ADD:           return "OP_ADD";
    case OP_SUB:           return "OP_SUB";
    case OP_MUL:           return "OP_MUL";
    case OP_DIV:           return "OP_DIV";
    case OP_MOD:           return "OP_MOD";
    case OP_NEG:           return "OP_NEG";
    case OP_INC:           return "OP_INC";
    /* OP_DEC reserved, never emitted (see vm.c). */
    case OP_EQ:            return "OP_EQ";
    case OP_NEQ:           return "OP_NEQ";
    case OP_LT:            return "OP_LT";
    case OP_LE:            return "OP_LE";
    case OP_GT:            return "OP_GT";
    case OP_GE:            return "OP_GE";
    case OP_NOT:           return "OP_NOT";
    case OP_STR_EQ:        return "OP_STR_EQ";
    case OP_JUMP:          return "OP_JUMP";
    case OP_JUMP_IF_FALSE: return "OP_JUMP_IF_FALSE";
    case OP_JUMP_IF_TRUE:  return "OP_JUMP_IF_TRUE";
    case OP_LOOP:          return "OP_LOOP";
    case OP_HALT:          return "OP_HALT";
    case OP_CALL:          return "OP_CALL";
    case OP_RETURN:        return "OP_RETURN";
    case OP_RETURN_V:      return "OP_RETURN_V";
    case OP_CALL_BUILTIN:  return "OP_CALL_BUILTIN";
    case OP_UNWRAP:        return "OP_UNWRAP";
    case OP_IS_NIL:        return "OP_IS_NIL";
    case OP_NIL_COALESCE:  return "OP_NIL_COALESCE";
    case OP_IF_LET:        return "OP_IF_LET";
    case OP_STR_CONCAT:    return "OP_STR_CONCAT";
    case OP_STR_LEN:       return "OP_STR_LEN";
    case OP_STR_INTERP_I:  return "OP_STR_INTERP_I";
    /* OP_STR_INTERP_B/O reserved, never emitted (see vm.c). */
    case OP_NEW_ARRAY:     return "OP_NEW_ARRAY";
    case OP_ARR_GET:       return "OP_ARR_GET";
    case OP_ARR_SET:       return "OP_ARR_SET";
    case OP_ARR_LEN:       return "OP_ARR_LEN";
    case OP_ARR_APPEND:    return "OP_ARR_APPEND";
    case OP_RETAIN:        return "OP_RETAIN";
    case OP_RELEASE:       return "OP_RELEASE";
    case OP_TRAP:          return "OP_TRAP";
    default:               return "OP_???";
  }
}

void profile_init(void) {
  if (!s_atexit_armed) {
    atexit(profile_dump);
    s_atexit_armed = 1;
  }
  /* Counters persist across vm_run calls (REPL accumulates); the
   * dump-at-exit captures the whole-session total. Explicit reset is
   * not exposed — bench programs are file-mode one-shots. */
}

void profile_tick(unsigned char op) {
  s_counts[op]++;
  s_total++;
}

/* Simple selection sort of opcode indices by descending count.
 * 256 entries × constant work per pass is fine for a profile dump. */
static void rank_opcodes(unsigned char *order, unsigned int *seen_out) {
  unsigned int i, j, seen;
  unsigned char tmp;
  for (i = 0; i < 256; ++i) order[i] = (unsigned char)i;
  seen = 0;
  for (i = 0; i < 256; ++i) if (s_counts[i] > 0) ++seen;
  /* selection sort the top entries to the front */
  for (i = 0; i < seen; ++i) {
    unsigned int best = i;
    for (j = i + 1; j < 256; ++j) {
      if (s_counts[order[j]] > s_counts[order[best]]) best = j;
    }
    if (best != i) {
      tmp = order[i];
      order[i] = order[best];
      order[best] = tmp;
    }
  }
  *seen_out = seen;
}

void profile_dump(void) {
  unsigned char order[256];
  unsigned int seen;
  unsigned int i;
  unsigned int top;
  double pct;

  if (s_total == 0) return;

  rank_opcodes(order, &seen);

  fprintf(stderr, "--- swiftii vm dispatch profile ---\n");
  fprintf(stderr, "total dispatched: %lu opcodes across %u distinct ops\n",
          s_total, seen);
  fprintf(stderr, "rank   count       pct    opcode\n");

  top = (seen < 20u) ? seen : 20u;
  for (i = 0; i < top; ++i) {
    unsigned char op = order[i];
    pct = (double)s_counts[op] * 100.0 / (double)s_total;
    fprintf(stderr, "%4u %8lu %7.2f%%   %s ($%02X)\n",
            i + 1u, s_counts[op], pct, opname(op), (unsigned)op);
  }
  fprintf(stderr, "--- end profile (top %u of %u) ---\n", top, seen);
}

#else

/* Empty TU when SWIFTII_PROFILE is undefined. cc65 and the regular
 * host build land here. A C89 compiler requires a non-empty TU; a
 * single typedef satisfies that without emitting any object code. */
typedef int swiftii_profile_empty_tu_t;

#endif /* SWIFTII_PROFILE */
