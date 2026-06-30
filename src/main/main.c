/* SwiftII entry point.
 *
 * Host: argv[1] selects file mode; no args drops into the REPL.
 * Apple II: the boot launcher (SWIFTII.SYSTEM on disk; see
 * tools/apple2/boot_launcher/) chains to this entry point in SWIFTIIP,
 * SWIFTIIE, SWIFTSAT, or SWIFTAUX. If the launcher staged a .swift file in
 * low RAM, repl_run() consumes it once before dropping to the prompt; with no
 * staged source this is just the interactive REPL.
 */

#include <stdint.h>

#include "../common/errors.h"
#include "../platform/platform.h"
#include "../file_runner/file_runner.h"
#include "../repl/repl.h"

#ifdef WITH_SWIFTSAT
/* xlc_init patches the call_xlc_smoke trampoline's Saturn-slot
 * switch byte (and locks Saturn bank 1 in R-RAM, write-protect).
 * Defined in src/platform/apple2/xlc.s; linked only into SWIFTSAT.
 * The Saturn slot was deposited at $1B04 by the boot launcher's
 * a_install_and_chain_swiftsat before LOADER ran — see
 * tools/apple2/boot_launcher/boot_launcher_asm.s. */
extern void __fastcall__ xlc_init(uint8_t saturn_slot);
#endif

#ifdef WITH_SWIFTAUX
/* aux_init is a reserved no-op init hook (peer to SWIFTSAT's xlc_init); the
 * XLC park lives in aux main RAM staged by the boot launcher's aux loader and
 * the trampoline uses ROM AUXMOVE, so there is no Saturn slot to patch. The
 * ProDOS /RAM disk that overlaps that aux park is unhooked by the boot
 * launcher (a_unhook_ram), which covers every aux build in one place. Defined
 * in src/platform/apple2/aux_xlc.s; linked only into SWIFTAUX. */
extern void __fastcall__ aux_init(void);
#endif

int main(int argc, char **argv) {
  int rc;

  if (platform_init() != 0) return 1;

#ifdef WITH_SWIFTSAT
  xlc_init(*(volatile uint8_t *)0x1B04);
#endif
#ifdef WITH_SWIFTAUX
  aux_init();
#endif

#if !defined(__CC65__)
  if (argc >= 2) {
    rc = file_runner_run(argv[1]);
    platform_shutdown();
    return rc;
  }
#else
  (void)argc;
  (void)argv;
#endif

  rc = repl_run();
  platform_shutdown();
  return rc;
}
