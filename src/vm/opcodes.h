/* Bytecode opcode enum.
 *
 * Mirrors docs/contributing/OPCODES.md (the source of truth). When adding an opcode,
 * update docs/contributing/OPCODES.md first, get review, then update this header.
 *
 * Some symbols below are reserved for bytecode stability even when the
 * compiler does not emit them and the VM has no dispatch case yet. OPCODES.md
 * records that implementation status per opcode.
 */
#ifndef SWIFTII_OPCODES_H
#define SWIFTII_OPCODES_H

typedef unsigned char op_t;

/* Constants and immediates */
#define OP_NIL          0x00
#define OP_TRUE         0x01
#define OP_FALSE        0x02
#define OP_INT_U8       0x03
#define OP_INT_I16      0x04
#define OP_CONST        0x05
#define OP_STR          0x06
#define OP_OPT_SOME     0x07
#define OP_OPT_NONE     0x08

/* Stack manipulation */
#define OP_POP          0x10
#define OP_DUP          0x11
#define OP_SWAP         0x12
#define OP_OVER         0x13
#define OP_POP_N        0x14

/* Variables. $25/$26 are reserved for OP_GET_GLOBAL_W / OP_SET_GLOBAL_W
 * if MAX_GLOBALS ever exceeds 256; today every tier stays below that
 * (Family A 32, Family B 48). */
#define OP_GET_GLOBAL    0x20
#define OP_SET_GLOBAL    0x21
#define OP_DEFINE_GLOBAL 0x22
#define OP_GET_LOCAL     0x23
#define OP_SET_LOCAL     0x24

/* Arithmetic */
#define OP_ADD  0x30
#define OP_SUB  0x31
#define OP_MUL  0x32
#define OP_DIV  0x33
#define OP_MOD  0x34
#define OP_NEG  0x35
#define OP_INC  0x36
#define OP_DEC  0x37

/* Comparison and logic */
#define OP_EQ     0x40
#define OP_NEQ    0x41
#define OP_LT     0x42
#define OP_LE     0x43
#define OP_GT     0x44
#define OP_GE     0x45
#define OP_NOT    0x46
#define OP_STR_EQ 0x47

/* Control flow */
#define OP_JUMP          0x50
#define OP_JUMP_IF_FALSE 0x51
#define OP_JUMP_IF_TRUE  0x52
#define OP_LOOP          0x53
#define OP_HALT          0x54

/* Function calls */
#define OP_CALL          0x60
#define OP_RETURN        0x61
#define OP_RETURN_V      0x62
#define OP_CALL_BUILTIN  0x63

/* Optional handling */
#define OP_UNWRAP        0x70
#define OP_IS_NIL        0x71
#define OP_NIL_COALESCE  0x72
#define OP_IF_LET        0x73

/* Heap object operations */
#define OP_STR_CONCAT    0x80
#define OP_STR_LEN       0x81
#define OP_STR_INTERP_I  0x82
#define OP_STR_INTERP_B  0x83
#define OP_STR_INTERP_O  0x84
#define OP_NEW_ARRAY     0x85
#define OP_ARR_GET       0x86
#define OP_ARR_SET       0x87
#define OP_ARR_LEN       0x88
#define OP_ARR_APPEND    0x89
/* $8A-$8F free. Array methods (removeLast/removeAll/contains)
 * ship as extras builtins (see BUILTIN_ARR_* below), not
 * dedicated opcodes — a MAIN-resident opcode case overflowed the lite
 * binary by ~840 B (no XLC arena on lite). */

/* Refcount management */
#define OP_RETAIN        0x90
#define OP_RELEASE       0x91

/* Trap / illegal opcode */
#define OP_TRAP          0xFF

/* Builtin function ids (for OP_CALL_BUILTIN). */
#define BUILTIN_PRINT     0x00
/* Print(_:terminator:) — argc-th arg is a string used as the line terminator
 * instead of "\n". The compiler accepts any expression there; the VM expects
 * the top of the arg run to be T_STR. */
#define BUILTIN_PRINT_T   0x01
/* ReadLine() -> String? — reads one line from stdin, strips
 * the trailing '\n' if present, and pushes either a T_STR (some) or
 * T_OPT_NIL (none) at EOF. */
#define BUILTIN_READLINE  0x02
/* Core Tier 2 builtins. min/max take two Ints and return
 * the smaller/larger. */
#define BUILTIN_MIN       0x03
#define BUILTIN_MAX       0x04
/* Phase 16: random(in: a..<b) / random(in: a...b) — Int
 * RNG, half-open / closed range. Reserved c7, shipped 2026-06-13
 * as a Family B (WITH_BIGLANG) builtin. Two Int args (lower, upper); the LCG
 * worker is builtin_random_in (runtime/builtins.c). */
#define BUILTIN_RANDOM_LT 0x05
#define BUILTIN_RANDOM_LE 0x06
/* String(_ n: Int) -> String emits OP_STR_INTERP_I directly and
 * consumes no builtin slot. (0x07 once also held a reservation for
 * BUILTIN_STR_TO_INT, scoped c8a but pulled on cc65 budget; that
 * re-landed as an XLC builtin — BUILTIN_STR_TO_INT below, $11.) */

/* abs/sgn — pure Int -> Int math, twins of min/max. They sit in the
 * core id range (< BUILTIN_XLC_FIRST) and ride the shared core-builtin
 * dispatcher, but they are FAMILY B ONLY: the recognizer is gated
 * WITH_SWB||host, so the standalone Compiler + host emit them and the
 * Runner executes them, while the REPLs never see them and stay
 * byte-identical. (Core-everywhere was attempted and dropped: the II+
 * lite REPL — no feature flags, no XLC bank — overflowed MAIN by ~280 B
 * with abs/sgn, and its only cuttable code is real features more useful
 * than abs/sgn. Same ceiling that kept wait/tone Family-B-only.) abs(x)
 * is real Swift; sgn(x) is a BASIC-flavored free function (Swift's form
 * is x.signum()). */
#define BUILTIN_ABS       0x07
#define BUILTIN_SGN       0x08

/* Reserves $09-$0C for former Apple II memory + screen builtin slots.
 * home / htab / vtab were attempted originally
 * (2026-05-24) and pulled — cc65 produced ~150-250 B per builtin in
 * the OP_CALL_BUILTIN else-if chain vs ~50-80 B source estimates.
 * peek + poke shipped briefly under the same sub-slice (commit
 * 01e65d3) but were unshipped 2026-05-27 alongside the
 * SWIFTSAT loader work — they belong to it anyway, and their
 * 163 B in the extras MAIN segment is reclaimed budget for the
 * Bank-switch infrastructure (see design doc 011). All five
 * slots stay reserved as historical gaps; the shipped screen/memory
 * builtins use XLC ids below. */
/* 0x07 + 0x08 are now BUILTIN_ABS / BUILTIN_SGN (see above); 0x09-0x0C
 * stay free.
 * 0x08-0x0C were reserved for the screen/memory builtins on
 * the assumption they would land in MAIN (all binaries).
 * Opened (2026-05-30) with the decision to ship them as extras XLC
 * builtins instead — lite (115 B headroom) can't absorb the
 * ~163 B+ MAIN cost, and the XLC path requires an id ≥ BUILTIN_XLC_FIRST
 * (the dispatcher in vm.c routes by that threshold). So home/peek/poke
 * take XLC ids 0x18-0x1A below; htab/vtab take 0x1B-0x1C; 0x09-0x0C
 * stay free. normal/inverse/flash, if ever shipped, also go XLC. See
 * design doc 012. */
/* 0x09-0x0C reserved (unused; see note above) */

/* XLC builtin range — ids ≥ BUILTIN_XLC_FIRST go
 * through the generic dispatch path (xlc_table.s + call_xlc_dispatch
 * in xlc.s). Adding a new XLC builtin: pick the next free id at the
 * top of the range, add a .word entry in xlc_table.s, write the
 * worker + dispatcher in builtins_xlc.c. The MAIN-side cost is just
 * the parser branch (~30-80 B); the dispatch table entry is 3 B in
 * XLC. See design doc 011 for the bus-switching mechanism and
 * commits 3b-3d for the pattern's evolution. */
#define BUILTIN_XLC_FIRST 0x0D
#define BUILTIN_ASC       0x0D
/* `chr(_ n: Int) -> String` — pairs with
 * BUILTIN_ASC via the same XLC path. Worker + dispatcher in
 * src/vm/builtins_xlc.c, table slot 1 in xlc_table.s. */
#define BUILTIN_CHR       0x0E

/* Internal XLC dispatch ids — NOT user-facing builtins and never
 * emitted in bytecode. They reuse the same XLC dispatch table +
 * trampoline (keyed by `id - BUILTIN_XLC_FIRST`) so a heavy, *cold*
 * VM opcode case body can be relocated out of MAIN into Saturn bank 1
 * on expanded builds, reclaiming MAIN headroom. The owning opcode's vm.c case
 * shrinks to `rc = XLC_CALL(<id>, 0)`. Lite keep the body inline in
 * MAIN, so the relocation is an extras MAIN win, not a code-size dedup.
 * First mover:
 * OP_STR_CONCAT (table slot 2). See design doc 011 § opcode relocation. */
#define XLC_OP_STR_CONCAT 0x0F
/* OP_STR_INTERP_I body (table slot 3) — second relocated opcode. */
#define XLC_OP_STR_INTERP 0x10
/* `Int(_ s: String) -> Int?` — user builtin, table
 * slot 4. Worker + dispatcher in builtins_xlc.c. */
#define BUILTIN_STR_TO_INT 0x11

/* Array methods — extras XLC builtins (table slots 5-7).
 * Dispatched from the Pratt postfix dot-member handler as
 * `OP_CALL_BUILTIN <id> <argc>` where the receiver array is on the
 * stack: removeLast()/removeAll() argc=1 (array only), contains(v)
 * argc=2 (array + needle). Workers + dispatchers in builtins_xlc.c.
 * Absent from lite (parser branch gated), so a
 * MAIN-resident opcode case body never bloats those binaries. */
#define BUILTIN_ARR_REMOVE_LAST 0x12
#define BUILTIN_ARR_REMOVE_ALL  0x13
#define BUILTIN_ARR_CONTAINS    0x14

/* Relocated cold opcode bodies (internal XLC dispatch ids, table slots
 * 8-9), moved out of MAIN on SWIFTSAT to reclaim the headroom the
 * `If let` else / function-body work needed. Same dual-copy
 * rule as XLC_OP_STR_CONCAT: lite keep the body
 * inline in vm.c (no XLC). OP_NEW_ARRAY (literal construction) and
 * OP_ARR_LEN (`.count`) are cold — neither sits in a tight arithmetic
 * loop the way OP_ADD / OP_GET_LOCAL do. See design doc 011. */
#define XLC_OP_NEW_ARRAY 0x15
#define XLC_OP_ARR_LEN   0x16
/* OP_CALL_BUILTIN's core-builtin bodies (print / print_t / readLine /
 * min / max) relocated as one dispatcher (table slot 10), 2026-05-30
 * reclaim pass before `switch`. Unlike the per-opcode relocations this
 * one needs the actual builtin id too: it rides the `xlc_builtin_id`
 * MAIN-BSS transport (set right before the call), while argc rides the
 * usual xlc_argc slot. The generic XLC builtins (asc/chr/Int(s)/array
 * methods, ids >= BUILTIN_XLC_FIRST) keep their own table slots — only
 * the ids < BUILTIN_XLC_FIRST funnel through here. Dual-copy: lite keeps
 * the bodies inline in vm.c. */
#define XLC_OP_CALL_BUILTIN 0x17

/* Platform builtins — extras XLC builtins (table slots
 * 11-13), the same residency + lite gating as the
 * Asc/chr/array methods. Parser branches in builtin_calls.c
 * (gated for SWIFTSAT/SWIFTAUX/Family B/host); workers + dispatchers in
 * builtins_xlc.c. peek/poke do raw main-RAM access (visible from XLC
 * regardless of Saturn bank state); the cc65 path reads/writes the
 * address directly, the host path stubs (peek → 0, poke → no-op) so
 * the test suite stays deterministic. home() routes to the existing
 * platform_clear_screen(). Speaker click rides poke(49200, 0) for
 * free. See design doc 012. */
#define BUILTIN_HOME      0x18
#define BUILTIN_PEEK      0x19
#define BUILTIN_POKE      0x1A
/* Cursor positioning (XLC table slots 14-15). 1-based
 * column/row (htab 1..40, vtab 1..24) to match Applesoft; SE_RUNTIME on
 * out of range. Route through platform_htab/vtab (cc65 conio gotoxy;
 * host no-op). */
#define BUILTIN_HTAB      0x1B
#define BUILTIN_VTAB      0x1C
/* Low-res graphics (XLC table slots 16-19). Mixed
 * GR (Applesoft-standard: 40x40 graphics + 4 text lines). gr/text take
 * no args; color(n) sets the current color 0..15; plot(x,y) writes a
 * block (x 0..39, y 0..39). SE_RUNTIME on out-of-range color/plot.
 * Route through platform_gr/text/gr_color/gr_plot (cc65 soft switches +
 * direct GR-page writes; host no-op). */
#define BUILTIN_GR        0x1D
#define BUILTIN_TEXT      0x1E
#define BUILTIN_COLOR     0x1F
#define BUILTIN_PLOT      0x20
/* gr() is mixed (40x40 + 4 text lines); grFull() is full-screen 40x48
 * (no text window). The active mode sets plot's y bound (39 vs 47).
 * XLC table slot 20. */
#define BUILTIN_GR_FULL   0x21
/* GR line/read (XLC table slots 21-23). hlin/vlin
 * take two endpoints + a positional line coordinate (argc 3) and draw a run
 * in the current colour; scrn(x,y) reads the colour back
 * (0..15). y bounds follow the active gr/grFull mode, same as plot. */
#define BUILTIN_HLIN      0x22
#define BUILTIN_VLIN      0x23
#define BUILTIN_SCRN      0x24
/* 80-column text. text80() switches the console to 80 columns where the
 * active build has a supported device path: //e firmware (SWIFTIIE,
 * SWIFTAUX, //e Runner) or II+ Videx (SWIFTSAT, II+ Runner). It is a no-op
 * when the runtime card probe fails, and a push-nil no-op on builds compiled
 * without WITH_80COL. text() ($1E) gains the matching 80->40 revert. */
#define BUILTIN_TEXT80    0x25

/* SWIFTSAT-only synthetic XLC dispatch id for the REPL line read — NOT a Swift
 * builtin, just the next free XLC table slot (25 = id - BUILTIN_XLC_FIRST) after
 * text80. It lets the REPL run platform_read_line (and its bank-1 blinking
 * cursor) in Saturn bank 1 via call_xlc_dispatch (keyboard.c repl_read_line).
 * Reuses the 0x26 value safely: the file builtins below are WITH_SWB-only and
 * never reach the SWIFTSAT XLC table, so the two never coexist in one build. */
#if defined(WITH_SWIFTSAT)
#define XLC_OP_REPL_READLINE 0x26
#endif

/* Family B file I/O (doc 015). WITH_SWB-gated: recognized by the
 * standalone Compiler and run by the Runner (both MAIN-only, so they have
 * ProDOS MLI for real file access — the Family A REPL does not, its LC holds
 * the interpreter). The Runner handles these inline in the lite dispatch;
 * they are NOT in the XLC range and never reach the SWIFTSAT/SWIFTAUX path.
 *   readFile(_ path: String) -> String?   ($26) — file bytes, or nil on error
 *   writeFile(_ path: String, _ s: String) -> Bool  ($27) — true on success */
#define BUILTIN_READ_FILE  0x26
#define BUILTIN_WRITE_FILE 0x27

/* Follow-up — Family B file/directory CRUD (doc 017). Same
 * residency + gating as readFile/writeFile: WITH_SWB only, inline in the
 * Runner's lite dispatch, never in the XLC range. deleteDirectory is a
 * compile-time alias of deleteFile (DESTROY handles both; non-empty
 * directories refuse and return false).
 *   deleteFile(_ p)/deleteDirectory(_ p) -> Bool ($28)
 *   renameFile(_ old, _ new) -> Bool             ($29)
 *   fileExists(_ p) -> Bool                      ($2A)
 *   appendFile(_ p, _ s) -> Bool                 ($2B) — creates if absent
 *   createDirectory(_ p) -> Bool                 ($2C)
 *   listDirectory(_ p) -> [String]               ($2D) — empty on error */
#define BUILTIN_DELETE_FILE 0x28
#define BUILTIN_RENAME_FILE 0x29
#define BUILTIN_FILE_EXISTS 0x2A
#define BUILTIN_APPEND_FILE 0x2B
#define BUILTIN_CREATE_DIR  0x2C
#define BUILTIN_LIST_DIR    0x2D

/* System builtins — Family B *program* builtins: they ship ONLY on the Family
 * B Compiler + Runner (II+ and //e) — which recognise them via a cheap
 * platform-table row and execute them (one -128 B Runner-heap trim cleared the
 * +86 B BSS wait()'s dispatch added) — plus host. NO REPL ships them: these
 * pace/voice a compiled program, not an interactive prompt, and the REPLs are
 * at their MAIN ceiling anyway (SWIFTSAT is full at 5 B — too tight even for
 * tone()'s recognizer row, whose worker would otherwise fit the XLC bank;
 * SWIFTAUX had room but the symmetry with SWIFTSAT was kept). At a REPL, pace
 * with a counted loop.
 *
 *   wait(_ ms: Int) ($2E) — busy-wait ~ms milliseconds. cc65 loops the
 *                           monitor ROM WAIT ($FCA8) with a fixed ~1 MHz
 *                           calibration (the classic Apple II clock is
 *                           constant, so no runtime calibration is needed;
 *                           accelerator cards run it short). Host is a no-op
 *                           (deterministic tests). Millisecond grain is for
 *                           pacing, NOT sound — that's tone() below.
 *   tone(_ halfPeriod: Int, _ cycles: Int) ($2F) — square-wave speaker tone
 *                           (Phase 16). cc65 toggles the
 *                           1-bit speaker $C030 in a counted loop: halfPeriod
 *                           sets the delay between toggles (pitch — larger is
 *                           lower), cycles the number of full periods
 *                           (duration). Blocks for the whole tone. Touches no
 *                           ROM, so unlike wait() it needs no bank juggling.
 *                           Host is a no-op (deterministic tests).
 *
 * exit() (-> :quit) and heapAvailable() (-> :mem) were scoped out: they
 * duplicate existing REPL meta-commands and there is no II+ headroom for any
 * of them anyway. The ids are placed ABOVE the Family B file block ($26-$2D,
 * all intercepted before XLC routing) rather than in the contiguous per-slot
 * XLC range ($0D-$25, indexed by id - BUILTIN_XLC_FIRST). BUILTIN_SYS_FIRST
 * marks that split: ids in [BUILTIN_XLC_FIRST, BUILTIN_SYS_FIRST) take a per-id
 * XLC table slot; ids >= BUILTIN_SYS_FIRST ride the shared core-builtin
 * dispatcher (xlc_call_builtin_dispatch), so they need no xlc_table slot and
 * no SWIFTAUX copy-down overlay entry. */
#define BUILTIN_WAIT        0x2E
#define BUILTIN_SYS_FIRST   BUILTIN_WAIT
#define BUILTIN_TONE        0x2F
/* hasPrefix / hasSuffix — String -> Bool methods (s.hasPrefix(t)),
 * Family B only: recognised in the .member parse path under WITH_SWB
 * (the heavy path that overflows lite, like the array methods), executed
 * by the Runner inline + host via xlc_call_dispatch. Two String args
 * (receiver + arg), Bool result; pure VM compare, no platform hook. */
#define BUILTIN_HAS_PREFIX  0x30
#define BUILTIN_HAS_SUFFIX  0x31

#endif /* SWIFTII_OPCODES_H */
