/* Runtime Apple II machine-type detection and hardware capabilities.
 *
 * Cached only the $FBB3 machine ID byte. (Design doc
 * 010) widens this to a `platform_capabilities_t` struct that the
 * extras-build heap allocator and the boot launcher branch on:
 *   - machine_type           : raw $FBB3 byte per Apple Technical Note #7
 *   - saturn_slot            : 1-7 if a Saturn 128K card is in that slot
 *   - has_aux_ram            : IIe 64K aux card present
 *   - has_80col              : IIe 80-column firmware responding
 *   - total_extra_ram_bytes  : saturn + aux, capped at 0xFFFF
 *   - is_extras_build        : set at compile time from WITH_EXTRAS
 *
 * Commit-2 scope (this file's current state): struct + extern declared;
 * the lite interpreter populates only `machine_type` from $FBB3 (status
 * quo behaviour). The Saturn / aux / 80-col probes live in the boot
 * launcher (commit 4) so the lite-binary footprint stays at its 27 B
 * headroom against the 40,704 B SYS ceiling. The extras interpreter
 * re-probes those fields itself when `WITH_EXTRAS` is defined; that
 * code lands in commit 4.
 *
 * Used by:
 *   - screen.c, to substitute `(:` / `:)` for `{` / `}` on pre-IIe
 *     machines whose character ROM has no glyphs for $7B / $7D (see
 *     docs/contributing/design/003-apple2-input-method.md).
 * - heap.c (extras build), to decide whether the
 *     two-arena allocator's arena 1 (Saturn) is usable.
 *
 * The original Apple ][ returns $38 and the II+ returns $EA at $FBB3
 * (Tech Note Misc #7) — two different bytes, both lacking lowercase /
 * brace glyphs in the character ROM. So the "pre-IIe" cohort is captured
 * by the APPLE_II_IS_PRE_IIE() test below, not by equality with a single
 * byte. (The interpreter cannot read the true ROM byte at runtime anyway;
 * see osdetect.c.)
 */
#ifndef SWIFTII_OSDETECT_H
#define SWIFTII_OSDETECT_H

#include <stdint.h>

/* Raw $FBB3 machine-ID bytes (Apple Tech Note Misc #7). Only the original
 * Apple ][ is $38; the II+ is $EA; the //e and every later model are $06
 * (sub-distinguished by $FBC0/$FBDD, which this codebase has no need for). */
#define APPLE_II_ORIG     0x38  /* original Apple ][ (and ][ J-Plus) */
#define APPLE_II_PLUS     0xEA  /* Apple ][+ */
#define APPLE_II_IIE      0x06  /* //e / //c / //c+ / IIgs */

/* Conservative pre-IIe default for machine_type (see osdetect.c): the
 * original-][ byte. Don't compare machine_type against this directly — the
 * pre-IIe cohort is the original ][ AND the II+ ($EA); use the test below. */
#define APPLE_II_PRE_IIE  APPLE_II_ORIG

/* True for the no-lowercase-glyph cohort: any machine that is not //e-class
 * ($06). An unknown/garbage byte counts as pre-IIe, so the inverse-video
 * case substitution fails safe (stays on). */
#define APPLE_II_IS_PRE_IIE(m)  ((m) != APPLE_II_IIE)

typedef struct {
  unsigned char machine_type;
  unsigned char saturn_slot;
  unsigned char has_aux_ram;
  unsigned char has_80col;
  uint16_t      total_extra_ram_bytes;
  unsigned char is_extras_build;
} platform_capabilities_t;

extern platform_capabilities_t platform_caps;

void osdetect_init(void);

#endif /* SWIFTII_OSDETECT_H */
