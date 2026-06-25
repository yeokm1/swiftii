/* Apple II runtime hardware-capability struct.
 *
 * machine_type is left at its conservative pre-IIe default; it is NOT probed
 * at runtime (see osdetect_init for why the $FBB3 read was unreliable). The
 * Saturn / aux RAM / 80-col fields were planned for an extras-build probe but
 * the boot launcher carries its own independent detection, so they stay zero.
 *
 * The struct gets a partial initializer so machine_type defaults to
 * APPLE_II_PRE_IIE — preserves the conservative pre-init behaviour
 * the old `platform_machine` byte had (pre-IIe inverse-video case-glyph
 * substitution stays on).
 *
 * Compiled only by cc65 for the apple2 target.
 */
#include "osdetect.h"

#include <stdint.h>

platform_capabilities_t platform_caps = { APPLE_II_PRE_IIE, 0, 0, 0, 0, 0 };

void osdetect_init(void) {
  /* No runtime $FBB3 read. The interpreter executes with the language card
   * banked in for reads, so a load from $FBB3 returns LC-RAM contents, not
   * the ROM machine-ID byte (Tech Note Misc #7 reads ROM). The pre-IIe vs
   * //e rendering choice is made at build time by WITH_IIE (design doc 003
   * rev 4); machine_type keeps its conservative pre-IIe default so screen.c's
   * inverse-video case substitution stays correct on the original ][ and the
   * II+ ($EA) alike. (DEBUG.SYSTEM loads into main RAM, banks ROM in with
   * `bit $C082`, and *does* read the true byte for its diagnostic.) */
#ifdef WITH_EXTRAS
  platform_caps.is_extras_build = 1;
#endif
}
