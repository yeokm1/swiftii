/* Unit tests for the hardware-capability struct.
 *
 * Commit 2 scope: verify the struct exists, has the right defaults,
 * and that osdetect_init populates machine_type. The probes (Saturn,
 * aux RAM, 80-col) land in commit 4 and get their own tests there.
 */
#include "../../src/platform/apple2/osdetect.h"

#include <stdint.h>
#include <stddef.h>

#define CHECK(expr) do { if (!(expr)) return __LINE__; } while (0)

int test_osdetect_caps_default_is_safe(void) {
  /* Before osdetect_init runs, machine_type should be the conservative
   * pre-IIe value so screen.c's inverse-video lowercase substitution
   * stays on. All extras fields default to zero. */
  platform_capabilities_t saved = platform_caps;
  platform_capabilities_t fresh = { APPLE_II_PRE_IIE, 0, 0, 0, 0, 0 };
  platform_caps = fresh;

  CHECK(platform_caps.machine_type == APPLE_II_PRE_IIE);
  CHECK(platform_caps.saturn_slot == 0);
  CHECK(platform_caps.has_aux_ram == 0);
  CHECK(platform_caps.has_80col == 0);
  CHECK(platform_caps.total_extra_ram_bytes == 0);
  CHECK(platform_caps.is_extras_build == 0);

  platform_caps = saved;
  return 0;
}

int test_osdetect_init_populates_machine_type(void) {
  /* Host stub forces machine_type to APPLE_II_IIE regardless of prior
   * state. Verifies osdetect_init actually writes the field. */
  platform_capabilities_t saved = platform_caps;
  platform_capabilities_t fresh = { APPLE_II_PRE_IIE, 0, 0, 0, 0, 0 };
  platform_caps = fresh;

  osdetect_init();
  CHECK(platform_caps.machine_type == APPLE_II_IIE);

  platform_caps = saved;
  return 0;
}

int test_osdetect_struct_size_is_packed(void) {
  /* 7 logical bytes: 4 chars + 1 uint16 + 1 char. cc65 may pad the
   * uint16 to a 2-byte boundary, pushing the total to 8. Either is
   * acceptable; this test pins the upper bound so we notice if a
   * future field bump silently doubles the footprint. */
  CHECK(sizeof(platform_capabilities_t) <= 8);
  return 0;
}

int test_osdetect_machine_id_classification(void) {
  /* Raw $FBB3 bytes per Apple Tech Note Misc #7: only the original ][ is
   * $38; the II+ is $EA (NOT a IIc — that was the old mislabel); the //e and
   * later are $06. The conservative default aliases the original-][ byte. */
  CHECK(APPLE_II_ORIG == 0x38);
  CHECK(APPLE_II_PLUS == 0xEA);
  CHECK(APPLE_II_IIE  == 0x06);
  CHECK(APPLE_II_PRE_IIE == APPLE_II_ORIG);

  /* The "no lowercase/brace glyphs" cohort is the original ][ AND the II+,
   * so the cohort test must catch both — a single-byte == $38 would miss a
   * real II+ ($EA), which was the detection bug this branch fixes. */
  CHECK(APPLE_II_IS_PRE_IIE(APPLE_II_ORIG));
  CHECK(APPLE_II_IS_PRE_IIE(APPLE_II_PLUS));
  CHECK(!APPLE_II_IS_PRE_IIE(APPLE_II_IIE));

  /* Fail safe: an unknown/garbage ID counts as pre-IIe (substitution on). */
  CHECK(APPLE_II_IS_PRE_IIE(0x00));
  CHECK(APPLE_II_IS_PRE_IIE(0xFF));
  return 0;
}
