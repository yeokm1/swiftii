/* Compile-time configuration knobs.
 *
 * Sized to match the actual layout in docs/contributing/MEMORY_MAP.md. The
 * fixed-address layout in the same file is the target and is
 * not yet honored by the linker config.
 */
#ifndef SWIFTII_CONFIG_H
#define SWIFTII_CONFIG_H

#include "version.h"          /* SWIFTII_VERSION / _YEAR / _COPYRIGHT */

/* "Big language" stretch features (for-in over an array, switch,
 * random, ...). They compile in the Family B Compiler (and the host, for
 * tests) but NOT the at-ceiling Family A REPL / lite / extras binaries: a
 * deliberate per-feature dialect fork (see ROADMAP). The Compiler
 * cl65 line and HOST_CFLAGS define WITH_BIGLANG; the Family A builds do not.
 * Pure compiler-side codegen using existing opcodes, so the Runner needs no
 * matching gate (it only executes bytecode). */
#if defined(WITH_BIGLANG)
#define SWIFTII_BIGLANG 1
#else
#define SWIFTII_BIGLANG 0
#endif

/* random(in:) carries a separate gate from the rest of WITH_BIGLANG
 * because it costs budget on BOTH Family B binaries (a parser branch in the
 * Compiler + the xorshift worker/dispatch in the Runner), unlike for-in/switch
 * which are compiler-only and free on the Runner. Shipped to Family B
 * 2026-06-13: the Compiler funds it by reclaiming bytecode-arena slack
 * (FILE_BC_SIZE 2176->1834), the Runner by a multiply-free xorshift that pulls
 * no math library + a smaller SWB_IMAGE_SIZE. Defined for host + both Family B
 * binaries; never the at-ceiling Family A interpreters. */
#if defined(WITH_RANDOM)
#define SWIFTII_RANDOM 1
#else
#define SWIFTII_RANDOM 0
#endif

/* REPL function redefinition (ROADMAP). Allows `func foo(){...}` to
 * rebind an existing name with a `redef foo` notice instead of erroring. The
 * code is compiler-side (parse_func_decl + the funcs table), which can't
 * relocate to the Saturn XLC overlay, so it must sit in MAIN — and the II+
 * REPLs (SWIFTIIP/SWIFTSAT) are at the ProDOS file ceiling. So it ships on
 * the two //e REPL interpreters only: WITH_IIE selects the //e disk builds,
 * and !WITH_SWB excludes the Family B Compiler/Runner (which also build
 * -DWITH_IIE but compile whole files, where redefinition has no REPL meaning
 * and would only spend their tight budget). The II+ REPLs keep `:reset` as
 * the workaround; all the excluded binaries stay byte-identical. */
#if defined(WITH_IIE) && !defined(WITH_SWB)
#define SWIFTII_FUNC_REDEF 1
#else
#define SWIFTII_FUNC_REDEF 0
#endif

/* VM software stack: 3 bytes per slot. */
#define VM_STACK_SLOTS 32

/* Call stack: { saved_pc, saved_fp, fn_idx } per frame. */
#define VM_CALL_FRAMES 4

/* Source buffer/window (file mode). Raised from 512 B
 * per docs/contributing/design/004-demo-oriented-scope.md.
 *
 * Family A interpreters keep the 2 KB staged-source size. The Family B
 * Compiler overrides this to a 4 KB low-RAM streaming window; total source
 * size is disk-bounded, but one statement must fit the window. */
#ifndef FILE_SRC_SIZE
#define FILE_SRC_SIZE 2048
#endif

/* Compiled-bytecode buffer. Holds top-level program bytecode plus
 * (in REPL mode) the persistent function-bytecode arena that grows
 * arena at the front of the buffer — see compiler/bcbuf.c for the split.
 * The Compiler (output) and Runner (input, run-in-place) raise
 * this via -D for bigger programs; interpreters keep 1 KB. */
#ifndef FILE_BC_SIZE
#define FILE_BC_SIZE 1024
#endif

/* Symbol-table sizes. Raised per design doc 004.
 * (Doc 016): the Family B Compiler AND Runner raise
 * MAX_GLOBALS/MAX_FUNCS via -D for bigger programs — the two binaries
 * must agree (the .swb funcs section is index-coupled); Family A keeps
 * the defaults. */
#ifndef MAX_GLOBALS
#define MAX_GLOBALS 32
#endif
#define MAX_LOCALS 16
#ifndef MAX_FUNCS
#define MAX_FUNCS 16
#endif
#define IDENT_MAX 12

/* String pool: number of slots for inlined string constants.
 * Per docs/using/LANGUAGE.md limit table; also acts as the lower bound
 * for heap offsets so that a T_STR payload < STRING_POOL_SLOTS is
 * unambiguously a pool index. */
#define STRING_POOL_SLOTS 16

/* Run programs from disk. The boot-launcher mini-shell reads the
 * chosen .swift source (it has the budget; the lite interpreters at the
 * ProDOS ceiling do not), stages the bytes at STAGED_SRC_ADDR and the
 * length at STAGED_LEN_ADDR (0 = nothing staged), then chains the
 * interpreter, which compiles+runs the staged program once at startup
 * before dropping to the REPL. No MLI or :load lands in the interpreter
 * binaries. The addresses sit in low RAM that the chain READ (which
 * writes $2000+) leaves untouched; they must also dodge each loader's
 * scratch (the SWIFTSAT XLC stager uses $0800) — reconciled with the boot
 * launcher + linker configs when the launcher side lands. STAGED_LEN_ADDR sits in
 * the established $1B00 launcher->interpreter handoff page (the Saturn slot is
 * already parked at $1B04). STAGED_SRC_ADDR must not overlap that page:
 * $0C00 is clear of the text pages ($0400-$0BFF) and of both the
 * interpreter and the boot launcher (each $2000+), and it survives the lite
 * chain's $2000+ READ. The SWIFTSAT/SWIFTAUX loaders use $0800-$17FF as
 * XLC-staging scratch during the chain, so they stage post-copy-down (a
 * later slice); the interpreter read address ($0C00) is common to all. */
#define STAGED_LEN_ADDR 0x1B06u
#define STAGED_SRC_ADDR 0x0C00u

/* The boot launcher's Saturn probe (a_probe_saturn) parks the detected
 * Saturn slot number (0..7, or $FF for none) here before chaining, in the
 * established $1B00 handoff page. SWIFTSAT's xlc_init reads it to patch its
 * runtime trampoline; the Tier-2 Family B Compiler/Runner (-DBC_STORE_SATURN)
 * read it to patch saturn_bc.s's bank-select switches. */
#define SX_SAT_SLOT_ADDR 0x1B04u

/* On-target test sweep (design doc 018). Before chaining a Family B test,
 * TESTRUN.SYSTEM writes this 2-byte marker into the same $1B00 handoff page
 * (it survives the COMPILER->RUNNER chain exactly as the Saturn slot at $1B04
 * does). The Runner reads it at end-of-run: if set, it auto-advances after a
 * short interval (or a keypress) instead of waiting indefinitely, so the
 * FBTESTS sweep runs hands-off; the Runner clears it after reading so a later
 * normal run still pauses for a key. $1B08/$1B09 are clear of the slot ($1B04)
 * and the staged-length ($1B06/$1B07); DEBUG_PARK is up at $1B80. */
#define TESTRUN_MODE_ADDR   0x1B08u
#define TESTRUN_MODE_MAGIC0 0x54u   /* 'T' */
#define TESTRUN_MODE_MAGIC1 0x52u   /* 'R' */

/* When the boot launcher enters the in-process editor for an [E]dit, it hands
 * over which file to open by writing a length-prefixed ProDOS path here
 * (byte 0 = length, 0 = none -> scratch buffer). It reuses the staged-source
 * region because an edit stages no SOURCE (STAGED_LEN_ADDR is left 0); the
 * editor copies the path out before any of its own file I/O can reuse the
 * area. */
#define EDIT_PATH_ADDR STAGED_SRC_ADDR

/* Family B handoff. The Compiler reads the source path the
 * launcher stages at EDIT_PATH_ADDR (length-prefixed; same channel the
 * editor uses), or DEFAULT_SRC_PATH if none is staged. It writes the
 * bytecode to SWB_PATH (a fixed ProDOS-relative name resolved against the
 * current prefix); the Runner reads the same SWB_PATH. `.swb` is the
 * re-runnable artifact — the disk file IS the compiler->runner handoff
 * (no in-RAM blob a chain READ would clobber). */
#define DEFAULT_SRC_PATH "SOURCE.SWIFT"
#define SWB_PATH         "SWIFTII.SWB"
/* The Runner SYS file the Compiler chains after a successful compile (the
 * launcher installs it under this name on the Family B disk). */
#define SWB_RUNNER_PATH  "RUNNER.SYSTEM"
/* The boot launcher (auto-launched as this on disk). The Compiler (on a
 * compile error) and the Runner (when the program ends) chain back to it
 * instead of cold-rebooting, so it reopens the editor / file selector via its
 * LASTRUN note — same place the run was launched from. */
#define LAUNCHER_PATH    "SWIFTII.SYSTEM"

/* Heap (strings, arrays, future struct instances). Only
 * allocates strings here. Sized to match the docs/contributing/CONSTRAINTS.md heap
 * budget of ~6 KB; today we leave headroom in BSS for the
 * Binary, so start at 2 KB.
 * The Runner raises this via -D (its ~21 KB MAIN-only window is
 * mostly free) so file programs get a big runtime heap; the Compiler keeps
 * it modest (only the constant pool lives here at compile time). */
#ifndef HEAP_SIZE
#define HEAP_SIZE 2048
#endif

#endif /* SWIFTII_CONFIG_H */
