/* Host-side stub for the Apple II hardware-capability probe.
 *
 * Lets the unit-test runner exercise any code that branches on
 * `platform_caps` (the two-arena heap
 * allocator) without dragging in cc65-only memory pokes. Defaults to
 * a IIe with no extra RAM so the host build matches the most
 * permissive Apple II target.
 *
 * Compiled only under clang.
 */
#include "../apple2/osdetect.h"

platform_capabilities_t platform_caps = { APPLE_II_IIE, 0, 0, 0, 0, 0 };

void osdetect_init(void) {
  platform_caps.machine_type = APPLE_II_IIE;
}
