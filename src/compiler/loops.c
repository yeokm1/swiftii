/* Loop-context tracking for `break`.
 *
 * See loops.h for the contract. Storage is module-static (like
 * locals.c and funcs.c) so the Parser struct stays small.
 */
#include "loops.h"

#include <stdint.h>

static uint16_t s_break_sites[LOOPS_MAX_DEPTH][LOOPS_MAX_BREAKS_LOOP];
static uint8_t  s_break_counts[LOOPS_MAX_DEPTH];
static uint8_t  s_depth;

void loops_reset(void) {
  uint8_t i;
  uint8_t j;
  for (i = 0; i < (uint8_t)LOOPS_MAX_DEPTH; ++i) {
    s_break_counts[i] = 0;
    for (j = 0; j < (uint8_t)LOOPS_MAX_BREAKS_LOOP; ++j) {
      s_break_sites[i][j] = 0;
    }
  }
  s_depth = 0;
}

unsigned char loops_in_loop(void) {
  return (unsigned char)(s_depth > 0 ? 1 : 0);
}

int loops_enter(void) {
  if (s_depth >= (uint8_t)LOOPS_MAX_DEPTH) return -1;
  s_break_counts[s_depth] = 0;
  ++s_depth;
  return 0;
}

int loops_record_break_site(uint16_t placeholder_pos) {
  uint8_t cur;
  uint8_t count;
  if (s_depth == 0) return -1;
  cur = (uint8_t)(s_depth - 1);
  count = s_break_counts[cur];
  if (count >= (uint8_t)LOOPS_MAX_BREAKS_LOOP) return -1;
  s_break_sites[cur][count] = placeholder_pos;
  s_break_counts[cur] = (uint8_t)(count + 1);
  return 0;
}

void loops_exit(Parser *p) {
  uint8_t cur;
  uint8_t i;
  uint8_t count;
  if (s_depth == 0) return;
  cur = (uint8_t)(s_depth - 1);
  count = s_break_counts[cur];
  for (i = 0; i < count; ++i) {
    patch_jump_to_here(p, s_break_sites[cur][i]);
  }
  s_break_counts[cur] = 0;
  --s_depth;
}
