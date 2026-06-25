/* Builtin print implementation.
 *
 * Formats Int, Bool, nil, and pool-resident strings. Heap-allocated
 * strings (with escape sequences and interpolation) land; other
 * tags fall through to a placeholder that makes regressions visible.
 *
 * The integer formatter is hand-written rather than relying on printf, per
 * docs/contributing/CONSTRAINTS.md rule 3 (cc65's printf pulls in a multi-kilobyte runtime).
 */
#include "builtins.h"

#include <stdint.h>

#include "../common/config.h"
#include "array.h"
#include "heap.h"
#include "string_pool.h"
#include "../platform/platform.h"

#if SWIFTII_RANDOM
/* random(in:) — Phase 16 stretch. 16-bit xorshift PRNG.
 * Each draw folds in platform_entropy() (keypress-timing on the Apple II, 0 on
 * the host) so real runs differ while the host stays deterministic — exactly
 * the Applesoft model, where RND was reproducible until the monitor's KEYIN
 * loop perturbed the seed during keyboard input. The host's fixed seed keeps
 * the feature testable (tests/integration/025_random asserts a sequence).
 *
 * Uses a Marsaglia xorshift (shifts 7/9/8) rather than an LCG **specifically
 * to avoid a 16-bit multiply**: on cc65 `*` pulls in a runtime mul routine the
 * Family B Runner would otherwise have to carry (~150-200 B over its tiny
 * margin). The range map below likewise uses a *signed* `%` on a forced-
 * non-negative dividend so it reuses the signed divide already linked for
 * int_div/int_mod (arith.c) — no separate unsigned-divide routine. Seed is
 * non-zero; xorshift keeps it non-zero (period 65535). */
static uint16_t s_rng = 0x1234u;

swiftii_err_t builtin_random_in(int16_t a, int16_t b, unsigned char closed,
                                Value *out) {
  uint16_t span;   /* number of values in range; 0 means the full 2^16 */
  int16_t off;
  uint16_t r;
  if (closed) {
    if (b < a) return SE_RUNTIME;
    span = (uint16_t)((uint16_t)((uint16_t)b - (uint16_t)a) + 1u);
  } else {
    if (b <= a) return SE_RUNTIME;
    span = (uint16_t)((uint16_t)b - (uint16_t)a);
  }
  /* Fold in keypress-timing entropy: 0 on the host (sequence unchanged ->
   * deterministic test), the user's reaction time on the Apple II. Keep the
   * state non-zero — xorshift is stuck at 0. */
  s_rng ^= platform_entropy();
  if (s_rng == 0) s_rng = 0x1234u;
  s_rng ^= (uint16_t)(s_rng << 7);
  s_rng ^= (uint16_t)(s_rng >> 9);
  s_rng ^= (uint16_t)(s_rng << 8);
  /* Map into [0, span). Mask to a non-negative int16 so the signed `%`
   * yields [0, span) and reuses the already-linked signed divide. span == 0
   * is the full closed range a...a+65535 — any value qualifies. */
  if (span == 0) {
    off = (int16_t)s_rng;
  } else {
    off = (int16_t)((int16_t)(s_rng & 0x7FFFu) % (int16_t)span);
  }
  r = (uint16_t)((uint16_t)a + (uint16_t)off);
  out->tag = T_INT;
  out->lo = (unsigned char)(r & 0xFF);
  out->hi = (unsigned char)((r >> 8) & 0xFF);
  return SE_OK;
}
#endif

static void print_i16(int16_t n) {
  char buf[8];
  int idx;
  uint16_t un;
  unsigned char negative;

  idx = (int)(sizeof(buf) - 1);
  buf[idx] = '\0';

  if (n < 0) {
    negative = 1;
    /* Cast through unsigned so INT16_MIN negates without UB. */
    un = (uint16_t)(0u - (uint16_t)n);
  } else {
    negative = 0;
    un = (uint16_t)n;
  }

  if (un == 0) {
    buf[--idx] = '0';
  } else {
    while (un > 0) {
      buf[--idx] = (char)('0' + (un % 10));
      un = (uint16_t)(un / 10);
    }
  }
  if (negative) buf[--idx] = '-';

  platform_write_str(&buf[idx]);
}

void builtins_print_value(const Value *v) {
  uint16_t idx;
  int16_t n;
  const StringPoolEntry *entry;

  switch (v->tag) {
    case T_STR:
      idx = (uint16_t)v->lo | ((uint16_t)v->hi << 8);
      if (idx < (uint16_t)STRING_POOL_SLOTS) {
        entry = string_pool_get(idx);
        if (entry != (const StringPoolEntry *)0) {
          platform_write(entry->data, entry->len);
        }
      } else {
        unsigned char *p = heap_payload((heap_off_t)idx);
        uint16_t plen = heap_len((heap_off_t)idx);
        if (p != (unsigned char *)0) {
          platform_write((const char *)p, plen);
        }
      }
      break;

    case T_INT:
      n = (int16_t)((uint16_t)v->lo | ((uint16_t)v->hi << 8));
      print_i16(n);
      break;

    case T_BOOL:
      platform_write_str(v->lo ? "true" : "false");
      break;

    case T_NIL:
    case T_OPT_NIL:
      platform_write_str("nil");
      break;

    case T_ARR: {
      /* Inline `[elem0, elem1, ...]` print. Matches Swift's print()
       * format. The REPL echo composes `name: Array = [...]` by
       * calling into this same path, so the elements are emitted
       * uniformly. Nested arrays would recurse — demos
       * don't generate them and there's no recursion guard. */
      heap_off_t arr;
      uint16_t count;
      uint16_t i;
      Value elem;
      arr = (heap_off_t)VALUE_PAYLOAD_U16(*v);
      count = array_count(arr);
      platform_putc('[');
      for (i = 0; i < count; ++i) {
        if (i > 0) platform_write_str(", ");
        if (array_get(arr, i, &elem) == SE_OK) {
          builtins_print_value(&elem);
        }
      }
      platform_putc(']');
      break;
    }

    default:
      /* Fills in heap-string / array / interpolation. */
      platform_write_str("?");
      break;
  }
}

void builtins_print_newline(void) {
  platform_putc('\n');
}

/* builtins_type_name + builtins_write_ctype (the Value-tag and ctype
 * display-name renderers) were removed 2026-06-06 — their only caller,
 * `:list`, was trimmed to print `name = value` without the `: Type`
 * annotation. The ~550 B reclaimed funded the compound-assignment +
 * loop-local compiler fixes on the II+ binaries. */
