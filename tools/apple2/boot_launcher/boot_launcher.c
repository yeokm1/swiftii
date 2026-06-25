/* tools/apple2/boot_launcher/boot_launcher.c — SwiftII boot selector.
 *
 * ProDOS-launched SYS file at $2000. Detects Saturn 128K and IIe-style
 * aux RAM, lets the user override the auto-selected interpreter during a
 * 5-second countdown with [L]ite or the disk's extras key — [A]UX on the
 * //e disk, [S]ATURN on the II+ disk (case-insensitive) — then chains via
 * MLI OPEN/READ/CLOSE to one of:
 *
 *   SWIFTIIP.SYSTEM   — lite,   II+-disk build (II/II+, //+ typing model)
 *   SWIFTIIE.SYSTEM   — lite,   //e-disk build (//e+, native case+lowercase)
 *   SWIFTSAT.SYSTEM   — extras, for any machine with a Saturn 128K card
 *   SWIFTAUX.SYSTEM   — extras, for a //e with a 64K aux (ext-80-col) card
 *
 * The two lite names are mutually exclusive per disk (the launcher is built
 * `-DLITE_IIE` for the //e disk); each volume carries exactly one.
 *
 * Detection-driven routing: we don't trust the $FBB3 machine-ID byte
 * because emulator presets (Mariani's "Apple II Plus") report e.g. IIc
 * on a 64 KB config. The honest signal is "do you have writable RAM
 * beyond the 64 KB base?" — that's Saturn-or-aux.
 *
 * High-level logic in C; low-level (ROM calls, soft-switch sequences,
 * MLI dispatcher, ZP bouncer) lives in boot_launcher_asm.s. See
 * boot_launcher.cfg for the linker config and docs/contributing/LESSONS.md 2026-05-25 /
 * 2026-05-26 for the back-story on the LC state, $FBB3 quirks,
 * pre-IIe COUT character mapping, and slot-card I/O addressing.
 */

#include <stdint.h>

/* Project version string (kept in src/common/config.h alongside the
 * REPL's banner). Including it here keeps the boot launcher's displayed
 * version in lockstep with the interpreters it chains to. */
#include "../../../src/common/config.h"

/* Merge: the editor is now linked into the launcher (BOTH disks),
 * entered in-process by the file browser (open_in_editor -> editor_main). */
#include "../../../src/editor/editor.h"

/* -------------------------------------------------------------------
 * Globals shared with boot_launcher_asm.s. cc65 exposes these to asm as
 * `_g_saturn_slot`, `_g_aux_found`, `_g_open_pathname`, `_g_open_refnum`.
 * ------------------------------------------------------------------- */

uint8_t g_saturn_slot;        /* $FF = none, 0..7 = slot where found      */
uint8_t g_aux_found;          /* 0 = no 64K aux (-> lite), 1 = real 64K aux (-> SWIFTAUX) */
uint8_t g_slot0_readback;     /* $D000 byte after slot-0 multi-bank probe */
uint8_t g_slot_readback;      /* $D000 byte from last slot 1..7 iteration */
const uint8_t *g_open_pathname; /* MLI OPEN pathname (length-prefixed)    */
uint8_t g_open_refnum;        /* MLI OPEN refnum out                      */

/* File-manager mini-UI path buffers (length-prefixed; byte 0
 * = length, bytes 1.. = ProDOS path/name in upper-case ASCII). Referenced
 * by the MLI param blocks in boot_launcher_asm.s. */
uint8_t g_prefix[65];         /* current ProDOS prefix (GET_PREFIX out)   */
uint8_t g_path[65];           /* action target / SET_PREFIX in / DESTROY  */
uint8_t g_path2[65];          /* RENAME new-name                          */
uint8_t g_boot_prefix[65];    /* prefix at boot (where the interpreters live) */

/* Resume note ("LASTRUN"). g_note is the serialized note body
 * ([Plen][prefix..][Nlen][name..][origin][curLo][curHi]); g_notepath is its
 * absolute path on the boot volume; g_note_len is the byte count handed to the
 * MLI WRITE. The origin byte is 1 when the run was launched from the
 * editor (resume reopens the editor on the file) or 0 from the file browser
 * (resume re-positions the browser); absent on older notes -> treated as 0. The
 * two trailing cursor bytes (little-endian) carry the editor's cursor offset so
 * an edit -> run -> edit round-trip reopens where the user left off; absent on
 * older notes -> 0 (top). */
uint8_t g_note[96];
uint8_t g_notepath[80];
uint16_t g_note_len;
/* Set by a run path before note_save: 1 = run came from the editor (Ctrl-R),
 * 0 = from the browser. Saved into the note so the post-reboot resume returns
 * to the right place (the program's run cold-reboots back to the launcher). */
static uint8_t g_run_from_editor;
/* Editor cursor offset for the note: set from editor_saved_cursor on the Ctrl-R
 * path (0 for browser runs), serialized by note_save, and parsed back by
 * note_parse for the resume (0 = top / older note). One global serves both the
 * write and read sides — they never overlap within a single boot. */
static uint16_t g_cursor;

/* Volume picker: ON_LINE writes 16-byte records here (one per online
 * device — byte 0 = unit<<4 | name-length, bytes 1.. = volume name). */
uint8_t g_online[256];

/* The one sticky text-width flag (0 = 40-col, 1 = 80-col
 * active). Shared with boot_launcher_asm.s (a_home/a_vtab take an 80-col arm)
 * as _g_width80, and read by the in-process editor's blit path. */
uint8_t g_width80;

#ifdef LITE_IIE
/* READ_BLOCK inputs for a_mli_read_block (the volume picker's free/total space
 * read). The asm helper patches these into the MLI param block before the call.
 * g_rb_unit is a ProDOS unit_num (DSSS0000); g_rb_block is the logical block.
 * //e-only: the disk-space readout ships on the //e launcher; the II+ launcher
 * drops it to protect its tight BSS budget (docs/contributing/ROADMAP.md). */
uint8_t  g_rb_unit;
uint16_t g_rb_block;
#endif

/* -------------------------------------------------------------------
 * boot_launcher_asm.s exports (all __fastcall__ so cc65 passes the single-
 * byte arg in A and reads the return value from A).
 * ------------------------------------------------------------------- */

extern void __fastcall__ a_home(void);
extern void __fastcall__ a_cout(uint8_t c);
extern void __fastcall__ a_cout_raw(uint8_t c);
extern void __fastcall__ a_vtab(uint8_t row);
extern void __fastcall__ a_wait(void);
extern uint8_t __fastcall__ a_kbd(void);
extern void __fastcall__ a_print_hex(uint8_t b);
extern void __fastcall__ a_probe_saturn(void);
extern void __fastcall__ a_probe_aux(void);
extern uint8_t __fastcall__ a_mli_open(void);
extern uint16_t __fastcall__ a_mli_read_startup(void);
extern void __fastcall__ a_install_and_chain(void);
extern void __fastcall__ a_install_and_chain_swiftsat(void);
extern void __fastcall__ a_install_and_chain_swiftaux(void);

/* File-manager MLI verbs (boot-launcher only). Each returns the MLI
 * error code (0 = success); the directory READ returns the 16-bit count. */
extern uint8_t  __fastcall__ a_mli_get_prefix(void);
extern uint8_t  __fastcall__ a_mli_set_prefix(void);
extern uint16_t __fastcall__ a_mli_read_dirblk(void);
extern uint8_t  __fastcall__ a_mli_close(void);
extern uint8_t  __fastcall__ a_mli_destroy(void);
extern uint8_t  __fastcall__ a_mli_rename(void);
extern uint8_t  __fastcall__ a_mli_create_dir(void);

/* Resume note ("LASTRUN") verbs. */
extern uint8_t  __fastcall__ a_mli_destroy_note(void);
extern uint8_t  __fastcall__ a_mli_create_note(void);
extern void     __fastcall__ a_mli_write_note(void);
extern uint16_t __fastcall__ a_mli_read_note(void);

/* Volume picker. */
extern uint8_t  __fastcall__ a_mli_online(void);

#ifdef LITE_IIE
/* READ_BLOCK g_rb_block from g_rb_unit into DIRBLK; returns the MLI error.
 * //e-only (powers the volume picker's free/total readout). */
extern uint8_t  __fastcall__ a_mli_read_block(void);
#endif

/* -------------------------------------------------------------------
 * Pathnames (ProDOS length-prefixed). Used directly by MLI via
 * g_open_pathname.
 * ------------------------------------------------------------------- */

/* The lite binary's on-disk name is machine-specific (design doc 003
 * rev 4): the II+ disk ships SWIFTIIP.SYSTEM (//+ typing model, caps),
 * the //e disk ships SWIFTIIE.SYSTEM (native case + lowercase). The
 * launcher is built once per disk — `-DLITE_IIE` for the //e disk — so its
 * fallback name matches what `build_po.sh` put on that volume. */
#ifdef LITE_IIE
static const uint8_t fname_lite[] = {
    15, 'S','W','I','F','T','I','I','E','.','S','Y','S','T','E','M'
};
#define LITE_DISPLAY "SWIFTIIE.SYSTEM"
/* The extras binary that may ship on a //e disk (SWIFTAUX). Its human label
 * (Help screen) + the front-page banner names; the launcher detects which of
 * the two binaries is actually on the booted disk (file_on_disk) and shows the
 * matching banner — there is exactly ONE interpreter per REPL disk. */
#define EXTRAS_LABEL  "//e aux 80-col"
#define LITE_BANNER   "SwiftII //e"
#define EXTRAS_BANNER "SwiftII //e aux"
/* Family B (compiler disk) banner — machine-tagged like the others. */
#define FAMILYB_BANNER "SwiftII Compiler //e"
/* Which disk this launcher was built for (Help / About). */
#define BUILD_NAME   "//e build"
#else
static const uint8_t fname_lite[] = {
    15, 'S','W','I','F','T','I','I','P','.','S','Y','S','T','E','M'
};
#define LITE_DISPLAY "SWIFTIIP.SYSTEM"
/* The extras binary that may ship on a II+ disk (SWIFTSAT). Label + banner
 * names (cout_str folds them to upper case on this II+ build); the launcher
 * shows whichever the booted disk actually carries (file_on_disk). */
#define EXTRAS_LABEL  "Saturn 128K"
#define LITE_BANNER   "SwiftII ][+"
#define EXTRAS_BANNER "SwiftII ][+ Saturn"
/* Two II+ compiler disks share this launcher source but ship different
 * COMPILER.SYSTEM/RUNNER.SYSTEM builds (flat Tier 1 vs Saturn-paged Tier 2) under
 * the SAME filenames, so the launcher can't tell them apart at run time (and a
 * HW Saturn probe would mislabel a flat disk booted on a Saturn machine). The
 * Saturn compiler disk therefore gets its own launcher build, -DFAMILYB_SATURN,
 * which tags the banner — exactly the build-flag-per-disk approach LITE_IIE uses. */
#ifdef FAMILYB_SATURN
#define FAMILYB_BANNER "SwiftII Compiler ][+ Saturn"
#else
#define FAMILYB_BANNER "SwiftII Compiler ][+"
#endif
#define BUILD_NAME   "][+ build"
#endif
/* cc65 toolchain version baked in by the Makefile (-DCC65_VER_STR, queried from
 * cl65 --version) so the About screen can't drift from the compiler that built
 * this binary. Fallback only matters if someone builds outside the Makefile. */
#ifndef CC65_VER_STR
#define CC65_VER_STR "?"
#endif
/* design doc 011. Saturn machines → SWIFTSAT.SYSTEM (XLC runs in place
 * in Saturn bank 1). //e-aux machines → SWIFTAUX.SYSTEM (slice
 * 2 — XLC bodies copied down from the aux park to STAGING per call).
 * Each 5.25" image carries ONE extras binary (four 40 KB binaries
 * overflow the 140 KB ProDOS template); the launcher falls back to lite if
 * its chosen extras binary isn't on the booted volume. (The legacy
 * unified SWIFTIIX.SYSTEM was retired 2026-05-31.) */
static const uint8_t fname_swiftsat[] = {
    15, 'S','W','I','F','T','S','A','T','.','S','Y','S','T','E','M'
};
static const uint8_t fname_swiftaux[] = {
    15, 'S','W','I','F','T','A','U','X','.','S','Y','S','T','E','M'
};
/* Family B (doc 015): a compiler disk carries COMPILER.SYSTEM +
 * RUNNER.SYSTEM instead of a REPL interpreter. When the launcher finds
 * COMPILER.SYSTEM on the booted volume it runs in Family B mode: a "run"
 * hands the Compiler the source PATH (at EDIT_PATH_ADDR) instead of staging
 * the source content, and the Compiler writes SWIFTII.SWB then chains
 * RUNNER.SYSTEM itself (chain.s). */
static const uint8_t fname_compiler[] = {
    15, 'C','O','M','P','I','L','E','R','.','S','Y','S','T','E','M'
};
/* The Runner — chained directly (skipping the Compiler) when a .swb is run
 * from the file selector; the Compiler chains it after compiling a .swift. */
static const uint8_t fname_runner[] = {
    13, 'R','U','N','N','E','R','.','S','Y','S','T','E','M'
};
/* TESTRUN.SYSTEM — the on-target auto-test harness (design doc 018). It lives
 * on the DATA disk (it needs that disk's TESTS/ tree; the tight program disks
 * have no room). The data disk's volume name is fixed by build_data_po.sh
 * (`SWIFTII.DATA`), so the launcher chains a constant absolute path — no
 * ON_LINE scan needed (the RAM-full launcher can't spare one). */
static const uint8_t fname_testrun[] = {
    28, '/','S','W','I','F','T','I','I','.','D','A','T','A','/',
        'T','E','S','T','R','U','N','.','S','Y','S','T','E','M'
};
/* The editor is now linked INTO this launcher (II+ disk build) and
 * entered in-process (open_in_editor -> editor_main), so there is no separate
 * SWIFTED.SYSTEM to chain by name. */

/* -------------------------------------------------------------------
 * Display helpers. a_cout() ORs $80 in for the Apple II high-bit-set
 * character convention, so string literals here are plain 7-bit ASCII.
 * ------------------------------------------------------------------- */

/* The UI text is written in proper mixed case. On the //e disk build (LITE_IIE)
 * the //e character ROM has lowercase glyphs, so it prints as-is. On the II+
 * disk build the pre-IIe ROM has NO lowercase ('a' | $80 = $E1 renders as '!'),
 * so fold ASCII letters to upper case before COUT — every II+ string thus stays
 * all-caps and readable, with no per-string #ifdef. */
static void cout_str(const char *s) {
#ifdef LITE_IIE
    while (*s) a_cout((uint8_t)*s++);
#else
    char c;
    while ((c = *s++)) {
        if (c >= 'a' && c <= 'z') c = (char)(c - ('a' - 'A'));
        a_cout((uint8_t)c);
    }
#endif
}

/* Print the build year — the last 4 chars of __DATE__ ("Mmm DD YYYY"). */
static void cout_year(void) {
    a_cout((uint8_t)__DATE__[7]);
    a_cout((uint8_t)__DATE__[8]);
    a_cout((uint8_t)__DATE__[9]);
    a_cout((uint8_t)__DATE__[10]);
}



/* -------------------------------------------------------------------
 * File-manager mini-UI. Option 3 opens an interactive two-pane
 * browser over the ProDOS directory tree:
 *
 *   - LEFT pane  = the parent directory's contents (read-only context;
 *                  the folder you are currently inside is marked '*').
 *   - RIGHT pane = the current directory, with the active '>' highlight.
 *   - DETAILS line under the panes shows the selected entry's ProDOS type
 *     (TXT/SYS/DIR/...) and size in bytes (folders show <FOLDER>).
 *
 * J/K (or the arrow keys) move the highlight; Return runs a file or enters
 * a folder; left-arrow exits to the parent; [E]dit / [N]ame / [D]elete /
 * [M]kdir act on the highlight; [Q] returns to the menu. All of it is
 * boot-launcher-only MLI work (the interpreter budget is untouched); see
 * docs/contributing/design/014-run-from-disk.md § "Plan — option 3".
 *
 * Big buffers live in free low RAM rather than BSS, so the launcher's runtime
 * footprint stays small: a directory block reads at $0800; the parsed
 * entries land in two 32-entry tables at $1400 (current) and $1680
 * (parent). Both tables sit ABOVE the $0C00..$13FF staged-source area, so
 * staging a chosen file for the run does not clobber them.
 * ------------------------------------------------------------------- */

#define DIRBLK    ((volatile uint8_t *)0x0800)  /* one 512 B ProDOS dir block */
#define CUR_TAB   ((uint8_t *)0x1400)           /* current-dir entries (32x20) */
#define PAR_TAB   ((uint8_t *)0x1680)           /* parent-dir entries  (32x20) */
#define ROW_BYTES 20
#define MAX_ROWS  32
#define PAGE_ROWS 9                             /* list rows 2..10 (preview pane below) */
#define CELL_W    17                            /* width of one pane column   */

/* Text-preview pane. The list occupies the top rows; a TXT file's
 * head previews in a permanent bottom split. The body is read into the
 * normally-free $0C00 staged-source region (only written by stage_file at
 * actual run time, and re-read fresh on chain), so no new buffer/BSS. The
 * body load is gated on a ~1.5 s dwell so fast navigation does not hit disk. */
#define PREVIEW         ((const uint8_t *)0x0C00) /* file head (<= 2 KB) */
#define PREVIEW_HDR_ROW 11                        /* name / type / size line   */
#define PREVIEW_TOP_ROW 12                        /* first body row (12..20)   */
#define PREVIEW_ROWS    9                         /* body rows                 */
#define PREVIEW_POS_ROW 21                        /* "LINE n-m/T" + scroll mark */
#define PREVIEW_W       39                        /* body chars/row (no col-39 wrap) */
#define PREVIEW_GUTTER  5                         /* line-number gutter "nnnn "  */
#define PREVIEW_TEXT_W  (PREVIEW_W - PREVIEW_GUTTER) /* source text cols/row     */
#define PREVIEW_TXT     0x04                      /* ProDOS TXT type (.swift)  */

/* Line-number gutter width for the loaded preview: enough columns for the
 * largest source line number's digits, plus one trailing space — exactly the
 * editor's editor_gutter_width(). Sized per file in load_preview, so a small
 * file's numbers hug the left edge instead of sitting in a fixed 4-digit field
 * (which read as offset from the left). Defaults to the old fixed width until a
 * file loads; never exceeds PREVIEW_GUTTER (a <=2 KB head tops out ~4 digits). */
static uint8_t s_prev_gutter = PREVIEW_GUTTER;

/* Effective preview body geometry. On the //e disk (LITE_IIE) the preview
 * spreads across the WHOLE screen when 80-col is active (g_width80): 78 cols
 * wide (no col-79 wrap), so a `.swift` file previews with far less soft-wrap.
 * On the 40-col path — and the entire II+ disk, where g_width80 is always 0 —
 * the body width stays the original 39 cols, so that build is byte-identical.
 * The text width subtracts the dynamic gutter in both builds. */
#ifdef LITE_IIE
#define PREVIEW_W80     78                        /* 80-col body width (no col-79 wrap) */
static uint8_t preview_body_w(void) { return g_width80 ? PREVIEW_W80 : PREVIEW_W; }
#else
#define preview_body_w()  PREVIEW_W
#endif
static uint8_t preview_text_w(void) {
    return (uint8_t)(preview_body_w() - s_prev_gutter);
}
#define DWELL_TICKS     30000u                    /* ~1.5 s idle poll (tunable)*/

/* Each table row (20 B):
 *   [0]      = (is_dir << 7) | name_length (1..15)
 *   [1..15]  = name, upper-case ASCII
 *   [16]     = ProDOS file_type
 *   [17..19] = logical size (EOF), little-endian 3-byte */
static uint8_t s_cur_count;   /* entries in CUR_TAB */
static uint8_t s_par_count;   /* entries in PAR_TAB */
static uint8_t s_sel;         /* highlighted row in the current pane */
static uint8_t s_cur_top;     /* first visible row (current pane scroll) */
static uint8_t s_curname[16]; /* current directory's own name (len-prefixed) */

/* Text-preview state (reset whenever the selection moves). */
static uint8_t  s_prev_done;   /* dwell handled for the current selection?   */
static uint8_t  s_prev_loaded; /* a TXT body is loaded into PREVIEW + shown  */
static uint16_t s_prev_len;    /* bytes read into PREVIEW                     */
static uint16_t s_prev_top;    /* first visible display-line (scroll offset)  */
static uint16_t s_prev_lines;  /* total display-lines in the buffer          */
static uint16_t s_prev_srctot; /* total source lines (gutter numbering)       */
static uint16_t s_prev_vis_a;  /* source-line number of the first visible row */
static uint16_t s_prev_vis_b;  /* source-line number of the last visible row  */
static uint8_t  s_prev_swift = 1; /* previewed file is .swift? (set in load_preview) */

static void prev_reset(void) { s_prev_done = 0; s_prev_loaded = 0; }

/* 1 if the row's filename ends in ".SWIFT" (a Swift source). Mirrors
 * row_is_swb. The pre-IIe preview's Swift-syntax rendering — the inverse-video
 * case swap and the `{ } |` digraph expansion — is gated on this, so a plain
 * text file (e.g. an all-caps README.TXT) previews natively. */
static uint8_t row_is_swift(const uint8_t *row) {
    uint8_t nn = (uint8_t)(row[0] & 0x0F);
    const uint8_t *nm = row + 1;
    if (nn < 6) return 0;
    nm += nn - 6;
    return (uint8_t)(nm[0] == '.' &&
                     (nm[1] == 'S' || nm[1] == 's') &&
                     (nm[2] == 'W' || nm[2] == 'w') &&
                     (nm[3] == 'I' || nm[3] == 'i') &&
                     (nm[4] == 'F' || nm[4] == 'f') &&
                     (nm[5] == 'T' || nm[5] == 't'));
}

/* Blocking key read, high bit masked off. */
static uint8_t getkey(void) {
    uint8_t k;
    do { k = a_kbd(); } while (!k);
    return (uint8_t)(k & 0x7F);
}

/* Print a length-prefixed ProDOS path (already upper-case). */
static void cout_path(const uint8_t *p) {
    uint8_t n = p[0], i;
    for (i = 0; i < n; i++) a_cout(p[1 + i]);
}

/* Print an unsigned 16-bit value in decimal. */
static void cout_u16(uint16_t v) {
    uint8_t buf[5], i = 0;
    if (!v) { a_cout('0'); return; }
    while (v) { buf[i++] = (uint8_t)('0' + (v % 10)); v /= 10; }
    while (i) a_cout(buf[--i]);
}

/* Decimal digit count of `v` (>=1), for padding around cout_u16. */
static uint8_t u16_digits(uint16_t v) {
    uint8_t d = 1;
    while (v >= 10) { v /= 10; d++; }
    return d;
}

#ifdef LITE_IIE
/* Print `v` in decimal right-justified to `w` columns (leading spaces), so the
 * volume picker's free/total fields line up regardless of digit count. */
static void cout_u16_rj(uint16_t v, uint8_t w) {
    uint8_t d = u16_digits(v);
    while (d++ < w) a_cout(' ');
    cout_u16(v);
}
#endif

/* Read a ProDOS name from the keyboard into a length-prefixed buffer with
 * a blinking inverse-block cursor. When `prefill` is set, dst's existing
 * length-prefixed value is shown first and is editable (used by rename so
 * the current name is pre-populated). Letters are upper-cased; backspace
 * edits; Return accepts; Esc cancels. Returns 1 on accept, 0 on cancel
 * (Esc) — on cancel dst is left unchanged. Names cap at 15 (ProDOS max). */
static uint8_t read_name(const char *prompt, uint8_t *dst, uint8_t prefill) {
    uint8_t n, k, blink = 0, i;
    uint16_t t = 0;
    a_cout('\r');
    cout_str(prompt);
    n = prefill ? dst[0] : 0;
    for (i = 0; i < n; i++) a_cout(dst[1 + i]);   /* echo the prefilled name */
    for (;;) {
        k = a_kbd();
        if (!k) {
            /* idle — blink a solid inverse-block cursor in place */
            if ((++t & 0x1FFF) == 0) {
                blink ^= 1;
                if (blink) a_cout_raw(0x20);      /* inverse space = block */
                else       a_cout(' ');           /* erase */
                a_cout(0x08);                      /* step back over it */
            }
            continue;
        }
        a_cout(' '); a_cout(0x08);                 /* clear the cursor cell */
        k &= 0x7F;
        if (k == 0x1B) { a_cout('\r'); return 0; } /* Esc = cancel */
        if (k == 0x0D) break;                      /* Return = accept */
        if (k == 0x08 || k == 0x7F) {              /* backspace */
            if (n) { n--; a_cout(0x08); a_cout(' '); a_cout(0x08); }
            continue;
        }
        if (n < 15) {
            if (k >= 'a' && k <= 'z') k = (uint8_t)(k - 0x20);
            dst[1 + n++] = k;
            a_cout(k);
        }
    }
    a_cout('\r');
    dst[0] = n;
    return 1;
}

/* Are two length-prefixed names equal? */
static uint8_t names_equal(const uint8_t *a, const uint8_t *b) {
    uint8_t i;
    if (a[0] != b[0]) return 0;
    for (i = 1; i <= a[0]; i++) if (a[i] != b[i]) return 0;
    return 1;
}

/* Copy a row's name into g_path (length-prefixed). ProDOS resolves it
 * against the current prefix for DESTROY/RENAME/OPEN. */
static void row_to_path(const uint8_t *row) {
    uint8_t nn = (uint8_t)(row[0] & 0x0F), i;
    g_path[0] = nn;
    for (i = 0; i < nn; i++) g_path[1 + i] = row[1 + i];
}

/* Add one directory entry to `table` (count in *pcnt) if there is room.
 * Every named entry is shown — subdirectories and files of any type, not
 * just `.swift` — so the browser can manage (rename/delete) anything on
 * the disk. Running a non-`.swift` file just stages its bytes as source. */
static void add_entry(uint8_t *table, uint8_t *pcnt, uint8_t storage,
                      const volatile uint8_t *entry) {
    uint8_t namelen = (uint8_t)(entry[0] & 0x0F);
    uint8_t is_dir  = (uint8_t)(storage == 0x0D);
    uint8_t *row;
    uint8_t i;
    if (!namelen) return;
    /* Hide the launcher's own 80-col preference file (SWIFT80, written by
     * pref_save on the boot volume) from the browser listing. */
    if (namelen == 7 &&
        entry[1] == 'S' && entry[2] == 'W' && entry[3] == 'I' &&
        entry[4] == 'F' && entry[5] == 'T' && entry[6] == '8' &&
        entry[7] == '0') return;
    if (*pcnt >= MAX_ROWS) return;
    row = table + (uint16_t)(*pcnt) * ROW_BYTES;
    row[0] = (uint8_t)(namelen | (is_dir ? 0x80 : 0x00));
    for (i = 0; i < namelen; i++) row[1 + i] = entry[1 + i];
    row[16] = entry[0x10];                       /* file_type */
    row[17] = entry[0x15];                        /* EOF size lo  */
    row[18] = entry[0x16];                        /* EOF size mid */
    row[19] = entry[0x17];                        /* EOF size hi  */
    (*pcnt)++;
}

/* OPEN the directory at `openpath` (absolute, no trailing slash), READ +
 * parse every block into `table`. Returns the MLI error code; the entry
 * count goes to *out_count. */
static uint8_t read_dir(const uint8_t *openpath, uint8_t *table,
                        uint8_t *out_count) {
    uint16_t xfer;
    uint8_t el = 0x27, epb = 0x0D, e, first = 1, err, cnt = 0;

    g_open_pathname = openpath;
    err = a_mli_open();
    if (err) { *out_count = 0; return err; }

    for (;;) {
        xfer = a_mli_read_dirblk();
        if (xfer < 4) break;
        if (first) {
            el  = DIRBLK[0x23];   /* entry_length      (header @ off 4) */
            epb = DIRBLK[0x24];   /* entries_per_block                  */
            if (el == 0 || epb == 0) { el = 0x27; epb = 0x0D; }
        }
        {
            uint16_t off = 4;
            for (e = 0; e < epb; e++) {
                const volatile uint8_t *entry = DIRBLK + off;
                if (!(first && e == 0)) {        /* slot 0 block 0 = header */
                    uint8_t storage = (uint8_t)(entry[0] >> 4);
                    if (storage != 0) add_entry(table, &cnt, storage, entry);
                }
                off += el;
            }
        }
        first = 0;
        if (xfer < 512) break;
    }
    a_mli_close();
    *out_count = cnt;
    return 0;
}

/* g_prefix minus its trailing '/', into dst (an openable absolute path). */
static void current_open_path(uint8_t *dst) {
    uint8_t n = g_prefix[0], i;
    for (i = 0; i <= n; i++) dst[i] = g_prefix[i];
    if (n > 1 && dst[n] == '/') dst[0] = (uint8_t)(n - 1);
}

/* The parent of g_prefix as an openable absolute path, into dst. Returns
 * 1 if a parent exists, 0 if g_prefix is the volume root. */
static uint8_t parent_open_path(uint8_t *dst) {
    uint8_t n = g_prefix[0], i, s;
    for (i = 0; i <= n; i++) dst[i] = g_prefix[i];
    if (n > 1 && dst[n] == '/') n--;            /* drop trailing slash */
    s = n;
    while (s > 0 && dst[s] != '/') s--;          /* slash before last comp */
    if (s <= 1) { dst[0] = 0; return 0; }        /* at volume root */
    dst[0] = (uint8_t)(s - 1);                    /* keep "/VOL" */
    return 1;
}

/* The current directory's own name (last path component of g_prefix),
 * into s_curname — used to mark it '*' in the parent pane. */
static void compute_curname(void) {
    uint8_t n = g_prefix[0], s, i, L;
    if (n && g_prefix[n] == '/') n--;            /* drop trailing slash */
    s = n;
    while (s > 0 && g_prefix[s] != '/') s--;      /* slash before the name */
    L = (uint8_t)(n - s);
    s_curname[0] = L;
    for (i = 0; i < L; i++) s_curname[1 + i] = g_prefix[s + 1 + i];
}

/* Populate both panes from the current prefix + its parent. Returns the
 * MLI error from reading the current directory (0 = ok). */
static uint8_t refresh_panes(void) {
    uint8_t err;
    err = a_mli_get_prefix();
    if (err) return err;
    compute_curname();
    current_open_path(g_path);
    err = read_dir(g_path, CUR_TAB, &s_cur_count);
    if (err) return err;
    if (parent_open_path(g_path))
        read_dir(g_path, PAR_TAB, &s_par_count); /* ignore err -> empty */
    else
        s_par_count = 0;
    return 0;
}

/* SET_PREFIX into the highlighted subdirectory: g_prefix + name + '/'. */
static uint8_t enter_dir(const uint8_t *row) {
    uint8_t pn = g_prefix[0];
    uint8_t nn = (uint8_t)(row[0] & 0x0F);
    uint8_t i, n = pn;
    if ((uint16_t)pn + nn + 1 > 64) return 0xFE;        /* path too long */
    for (i = 0; i <= pn; i++) g_path[i] = g_prefix[i];  /* prefix (w/ '/') */
    for (i = 0; i < nn; i++) g_path[1 + n++] = row[1 + i];
    g_path[1 + n++] = '/';
    g_path[0] = n;
    return a_mli_set_prefix();
}

/* SET_PREFIX to the parent of the current prefix. Returns 0xFF (no MLI
 * call made) when already at the volume root. */
static uint8_t exit_dir(void) {
    uint8_t n = g_prefix[0], i;
    for (i = 0; i <= n; i++) g_path[i] = g_prefix[i];
    if (n > 1 && g_path[n] == '/') n--;             /* drop trailing slash */
    while (n > 0 && g_path[n] != '/') n--;          /* back to parent slash */
    if (n <= 1) return 0xFF;                        /* "/VOL/" -> can't go up */
    g_path[0] = n;                                  /* keep through the slash */
    return a_mli_set_prefix();
}

/* Clear the bottom two screen rows (the action legend) and park the cursor at
 * the start of row 22, so a prompt drawn next (its own leading CR steps to row
 * 23) lands on freshly-cleared lines instead of scrolling the list and
 * clobbering the legend. The browser redraw on the next loop iteration repaints
 * the legend. */
static void clear_prompt_area(void) {
    uint8_t c;
    a_vtab(22);
    for (c = 0; c < 40; c++) a_cout(' ');   /* row 22 (wraps to row 23 col 0) */
    for (c = 0; c < 39; c++) a_cout(' ');   /* row 23 cols 0..38 (no scroll)  */
    a_vtab(22);
}

/* Report a failed MLI action and wait for a key. */
static void action_err(const char *what, uint8_t err) {
    clear_prompt_area();
    a_cout('\r');
    cout_str(what);
    cout_str(" failed (err $");
    a_print_hex(err);
    cout_str(") - key");
    getkey();
}

/* Re-read both panes after a mutating action; clamp the highlight/scroll.
 * If the re-read fails (a hard MLI error — e.g. GET_PREFIX bails before
 * read_dir can run, leaving s_cur_count untouched at its stale value), zero both
 * panes so the browser renders an empty listing rather than whatever stale bytes
 * sit in CUR_TAB/PAR_TAB. Those tables live in the editor's $0800-$1BFF gap-buf
 * window, so after an in-process edit "stale" means editor garbage — an empty
 * pane is the honest, recoverable degraded state ('Q' to the menu still works,
 * and a later navigation re-reads cleanly once MLI is healthy). */
static void reload_panes(void) {
    if (refresh_panes()) { s_cur_count = 0; s_par_count = 0; }
    if (s_sel >= s_cur_count) s_sel = s_cur_count ? (uint8_t)(s_cur_count - 1) : 0;
    if (s_cur_top > s_sel) s_cur_top = s_sel;
    if (s_sel >= (uint8_t)(s_cur_top + PAGE_ROWS))
        s_cur_top = (uint8_t)(s_sel - (PAGE_ROWS - 1));
}

/* Does a parent-pane row name the directory we are currently inside? */
static uint8_t row_is_curdir(const uint8_t *row) {
    uint8_t n = (uint8_t)(row[0] & 0x0F), i;
    if (!(row[0] & 0x80)) return 0;          /* only folders */
    if (n != s_curname[0]) return 0;
    for (i = 0; i < n; i++)
        if (row[1 + i] != s_curname[1 + i]) return 0;
    return 1;
}

/* Print one pane cell, padded to CELL_W. `mark` is the leading marker. */
static void print_cell(const uint8_t *table, uint8_t idx, uint8_t count,
                       char mark) {
    uint8_t w = 0;
    if (idx < count) {
        const uint8_t *r = table + (uint16_t)idx * ROW_BYTES;
        uint8_t n = (uint8_t)(r[0] & 0x0F), j;
        a_cout((uint8_t)mark);                 w++;
        a_cout((r[0] & 0x80) ? '/' : ' ');     w++;
        for (j = 0; j < n && w < CELL_W; j++) { a_cout(r[1 + j]); w++; }
    }
    while (w < CELL_W) { a_cout(' '); w++; }
}

/* The details line for the highlighted current-pane entry. */
static void print_type(uint8_t t) {
    switch (t) {
        case 0x04: cout_str("TXT"); break;
        case 0xFF: cout_str("SYS"); break;
        case 0x06: cout_str("BIN"); break;
        case 0xFC: cout_str("BAS"); break;
        case 0x0F: cout_str("DIR"); break;
        default:   a_cout('$'); a_print_hex(t); break;
    }
}

static void draw_details(void) {
    const uint8_t *r;
    uint8_t n, j;
    if (!s_cur_count) { cout_str("(empty)"); return; }
    r = CUR_TAB + (uint16_t)s_sel * ROW_BYTES;
    n = (uint8_t)(r[0] & 0x0F);
    for (j = 0; j < n; j++) a_cout(r[1 + j]);
    if (r[0] & 0x80) { cout_str("  <folder>"); return; }
    cout_str("  ");
    print_type(r[16]);
    cout_str("  ");
    if (r[19]) {
        cout_str(">64K");                       /* 3-byte EOF over 16 bits */
    } else {
        cout_u16((uint16_t)r[17] | ((uint16_t)r[18] << 8));
        cout_str(" B");
    }
}

/* -------------------------------------------------------------------
 * Text-preview rendering. Mirrors src/platform/apple2/screen.c emit() so a
 * `.swift` file previews the way it reads in the REPL/editor on this machine:
 *   - #ifndef LITE_IIE (II+/lower): uppercase -> inverse-video uppercase,
 *     lowercase -> normal-video uppercase; { } | -> their input-method
 *     digraphs <% %> ??! ; other printable $20..$5F as-is; control /
 *     no-glyph bytes -> space.
 *   - #ifdef LITE_IIE (//e/higher): native — lowercase $E0..$FF screen codes
 *     (ALTCHAR is set in file_browser); control bytes -> space.
 * No runtime machine probe — the disk build declares the machine (doc 003).
 * ------------------------------------------------------------------- */

/* Columns one source byte consumes once rendered (digraphs are 2-3 wide). */
static uint8_t preview_width(uint8_t c) {
#ifdef LITE_IIE
    (void)c;
    return 1;
#else
    if (s_prev_swift) {                  /* digraphs only widen for .swift */
        if (c == '{' || c == '}') return 2;
        if (c == '|') return 3;
    }
    return 1;
#endif
}

/* Render one printable source byte at the cursor. */
static void preview_putc(uint8_t c) {
#ifdef LITE_IIE
    if (c < 0x20) { a_cout(' '); return; }
    a_cout(c);                       /* ALTCHAR on -> $E0..$FF lowercase glyphs */
#else
    if (!s_prev_swift) {
        /* Plain text (e.g. README.TXT): native II+ display — no inverse-video
         * case swap and no `{ } |` digraph expansion, so an all-caps file reads
         * as plain normal-video text. $20..$5F render as-is; nothing else has a
         * pre-IIe glyph (an all-caps file has no lowercase to lose). */
        if (c >= 0x20 && c < 0x60) a_cout(c);
        else a_cout(' ');
        return;
    }
    if (c >= 'a' && c <= 'z') { a_cout((uint8_t)(c - 0x20)); return; }     /* normal-video uppercase */
    if (c >= 'A' && c <= 'Z') { a_cout_raw((uint8_t)(c - 0x40)); return; } /* inverse-video uppercase */
    if (c == '{') { a_cout('<'); a_cout('%'); return; }
    if (c == '}') { a_cout('%'); a_cout('>'); return; }
    if (c == '|') { a_cout('?'); a_cout('?'); a_cout('!'); return; }
    if (c < 0x20 || c >= 0x60) { a_cout(' '); return; }  /* no pre-IIe glyph */
    a_cout(c);
#endif
}

/* Print the line-number gutter for a body row: source line `n` right-aligned
 * in s_prev_gutter-1 columns then a space, or all-blank when `n` is 0 (a
 * wrapped continuation row, so the number shows only on a source line's first
 * row). s_prev_gutter is sized to the file in load_preview. */
static void preview_gutter(uint16_t n) {
    uint8_t pad;
    if (n == 0) {
        for (pad = 0; pad < s_prev_gutter; pad++) a_cout(' ');
        return;
    }
    for (pad = (uint8_t)(s_prev_gutter - 1); pad > u16_digits(n); pad--)
        a_cout(' ');
    cout_u16(n);
    a_cout(' ');
}

/* Single walk over the PREVIEW buffer applying the soft-wrap at preview_text_w()
 * (the width left after the dynamic line-number gutter). Counts total display-lines
 * and the total source-line count (s_prev_srctot); when `render` is set, also
 * draws the window [top, top+PREVIEW_ROWS) into the body rows — each visible
 * row gets the gutter (the source line number on a line's first row, blank on
 * wrapped rows) then the text — and records the source-line numbers of the
 * first/last visible rows (s_prev_vis_a / _b). A source $0D/$0A ends a source
 * line; a longer line wraps. We a_vtab each row and emit at most PREVIEW_W
 * cols, so COUT never auto-wraps/scrolls (which would corrupt the list above).
 * Returns the display-line count. */
static uint16_t preview_walk(uint16_t top, uint8_t render) {
    uint16_t i;
    uint16_t dl = 0;          /* current display line                       */
    uint16_t sl = 1;          /* current source line number                 */
    uint8_t  col = 0;         /* text column within the display line         */
    uint8_t  open = 0;        /* cursor already parked on this visible line? */
    uint8_t  newsrc = 1;      /* this display row starts a new source line?  */
    uint8_t  tw = preview_text_w(); /* wrap column (wider in 80-col mode)    */

    if (render) { s_prev_vis_a = 0; s_prev_vis_b = 0; }

    for (i = 0; i < s_prev_len; i++) {
        uint8_t c = PREVIEW[i];
        uint8_t w;
        if (c == 0x0D || c == 0x0A) {
            /* an empty source line still gets its own numbered row */
            if (render && !open && dl >= top &&
                dl < (uint16_t)(top + PREVIEW_ROWS)) {
                a_vtab((uint8_t)(PREVIEW_TOP_ROW + (uint8_t)(dl - top)));
                preview_gutter(sl);
                if (!s_prev_vis_a) s_prev_vis_a = sl;
                s_prev_vis_b = sl;
            }
            dl++; col = 0; open = 0; sl++; newsrc = 1;
            continue;
        }
        w = preview_width(c);
        if ((uint8_t)(col + w) > tw) { dl++; col = 0; open = 0; newsrc = 0; }
        if (render && dl >= top && dl < (uint16_t)(top + PREVIEW_ROWS)) {
            if (!open) {
                a_vtab((uint8_t)(PREVIEW_TOP_ROW + (uint8_t)(dl - top)));
                preview_gutter(newsrc ? sl : 0);
                if (!s_prev_vis_a) s_prev_vis_a = sl;
                s_prev_vis_b = sl;
                open = 1;
            }
            preview_putc(c);
        }
        col = (uint8_t)(col + w);
    }
    if (col > 0) dl++;                 /* trailing line with no closing newline */
    s_prev_srctot = (uint16_t)((sl - 1) + (col > 0 ? 1 : 0));
    return dl;
}

/* Dwell fired on the current selection: load a TXT file's head into PREVIEW
 * and count its display-lines. Reuses the run-staging read (OPEN +
 * a_mli_read_startup into $0C00, then CLOSE) but drops NO LASTRUN note. */
static void load_preview(const uint8_t *row) {
    s_prev_done = 1;
    s_prev_loaded = 0;
    s_prev_len = 0;
    s_prev_top = 0;
    s_prev_lines = 0;
    if (row[0] & 0x80) return;                 /* folder */
    if (row[16] != PREVIEW_TXT) return;        /* non-text -> no preview */
    s_prev_swift = row_is_swift(row);          /* gates the pre-IIe digraph/case render */
    row_to_path(row);
    g_open_pathname = g_path;
    if (a_mli_open() != 0) return;             /* unreadable -> no preview */
    s_prev_len = a_mli_read_startup();         /* <= 2 KB into $0C00, closes */
    /* Size the line-number gutter to the file (like editor_gutter_width) BEFORE
     * the walk, whose wrap width depends on it. The source-line total is
     * independent of wrapping, so this pre-scan (mirroring the walk's srctot:
     * #terminators + a trailing partial line) is enough. */
    {
        uint16_t i, sl = 1;
        uint8_t col = 0, digits;
        for (i = 0; i < s_prev_len; i++) {
            uint8_t c = PREVIEW[i];
            if (c == 0x0D || c == 0x0A) { sl++; col = 0; } else col = 1;
        }
        digits = (uint8_t)u16_digits((uint16_t)((sl - 1) + (col ? 1 : 0)));
        s_prev_gutter = (uint8_t)(digits + 1);   /* digits + trailing space */
    }
    s_prev_lines = preview_walk(0, 0);
    s_prev_loaded = 1;
}

/* The preview pane's status-bar footer: which source lines are visible (the
 * same numbers shown in the gutter) of the file's total. Call after a render
 * walk, which sets s_prev_vis_a/_b. */
static void draw_preview_pos(void) {
    a_vtab(PREVIEW_POS_ROW);
    cout_str("Line ");
    cout_u16(s_prev_vis_a);
    a_cout('-');
    cout_u16(s_prev_vis_b);
    a_cout('/');
    cout_u16(s_prev_srctot);
}

/* Blank `count` rows from row `from`, without scrolling, across the preview's
 * full body width (cols 0..38 in 40-col, 0..77 in 80-col — the widest the
 * preview ever draws). Used to wipe the preview region in place so a previous
 * file's preview doesn't linger when navigation (a no-clear redraw) resets it. */
static void blank_rows(uint8_t from, uint8_t count) {
    uint8_t c, w = preview_body_w();
    while (count--) {
        a_vtab(from++);
        for (c = 0; c < w; c++) a_cout(' ');
    }
}

/* Draw the preview pane: the header (name/type/size of the highlight) plus,
 * once loaded, the scrollable body and its status-bar footer (line position +
 * scroll indicator). Before the dwell it stays blank. The details row and body
 * are cleared first so they don't keep a previous highlight's text on a
 * no-clear (navigation) redraw. */
static void draw_preview(void) {
    blank_rows(PREVIEW_HDR_ROW, 1);                 /* clear stale details */
    a_vtab(PREVIEW_HDR_ROW);
    draw_details();
    if (s_prev_loaded) {
        /* Clear body + footer first: the walk draws each row's content but does
         * not pad it, so an IN-PLACE redraw (J/K preview scroll, where the rest
         * of the screen is left intact) would otherwise keep stale tails from
         * the previous, longer line at that row. Harmless (blank-over-blank) on
         * the dwell-load and full-redraw paths, where the body is already clear. */
        blank_rows(PREVIEW_TOP_ROW, PREVIEW_ROWS + 1);
        preview_walk(s_prev_top, 1);
        draw_preview_pos();
    } else {
        blank_rows(PREVIEW_TOP_ROW, PREVIEW_ROWS + 1);   /* body + footer row */
        if (s_prev_done) { a_vtab(PREVIEW_TOP_ROW); cout_str("(no preview)"); }
    }
}

/* Draw the two-pane browser: prefix header (row 0), the "PARENT | CURRENT"
 * column header with per-pane position (row 1), the list (rows 2..10), then
 * the preview pane (header row 11, body rows 12..20, status footer row 21)
 * and a two-line action legend (rows 22-23). The panes are space-separated
 * (no '|' divider: it has no pre-IIe glyph) so the right pane's '>' highlight
 * has clear air around it. */
static void draw_browser(uint8_t clear) {
    uint8_t i, k;
    /* Navigation passes clear=0: home the cursor WITHOUT wiping the screen and
     * overwrite in place, so moving the highlight doesn't flicker the whole
     * screen (the slow 1 MHz HOME + COUT redraw). Structural changes (entry,
     * dir change, rename/delete) pass clear=1. Same-directory navigation keeps
     * the same row count, so the panes overwrite cleanly; only the header
     * count, the details line and the preview body can go stale, and those are
     * padded/cleared explicitly below. */
    if (clear) a_home(); else a_vtab(0);
    cout_str("Dir: ");
    cout_path(g_prefix);
    a_cout('\r');

    /* Column header aligned to the cell layout (left cell is CELL_W wide,
     * then a 2-space gutter, then the right cell). No vertical-bar divider:
     * '|' ($7C) has no glyph on the pre-IIe (II+) character ROM — it renders
     * as garbage (a stray '<') — so the columns are separated by space, and
     * the PARENT/CURRENT headers + the right pane's '>' anchor the split.
     * Each header carries its pane's position: PARENT shows its entry count
     * (read-only context, shown from the top); CURRENT shows sel/total. */
    cout_str("Parent ");                            /* 7 */
    cout_u16(s_par_count);
    k = (uint8_t)(7 + u16_digits(s_par_count));
    while (k < CELL_W) { a_cout(' '); k++; }
    cout_str("  Current ");
    cout_u16(s_cur_count ? (uint16_t)(s_sel + 1) : 0);
    a_cout('/');
    cout_u16(s_cur_count);
    cout_str("   ");                             /* pad: clear a wider stale count */
    a_cout('\r');

    for (i = 0; i < PAGE_ROWS; i++) {
        uint8_t pidx = i;                       /* parent shown from the top */
        uint8_t cidx = (uint8_t)(s_cur_top + i);
        if (pidx >= s_par_count && cidx >= s_cur_count) break;
        print_cell(PAR_TAB, pidx, s_par_count,
                   (pidx < s_par_count &&
                    row_is_curdir(PAR_TAB + (uint16_t)pidx * ROW_BYTES))
                       ? '*' : ' ');
        cout_str("  ");                         /* gutter */
        print_cell(CUR_TAB, cidx, s_cur_count, (cidx == s_sel) ? '>' : ' ');
        a_cout('\r');
    }

    /* Preview pane: header (name/type/size) + the scrollable body. */
    draw_preview();

    /* Action legend on the bottom two rows. I/M move the highlight (//e
     * Up/Down too); Ctrl-T/Ctrl-V page it up/down a window; J/K scroll the
     * preview; `,` goes up a directory (or lists volumes at a disk root); `.` /
     * RET enter a folder. RET OPENS a .swift in the editor (or launches a
     * .SYSTEM file); [X] eXecutes the selected .swift with this disk's sole
     * interpreter. [R]/[D]/[N] = rename / delete / new folder. The last line has
     * no trailing CR so row 23 doesn't scroll; both stay <=39 cols (no wrap). */
    a_vtab(22);
    cout_str("I/M ^T/V=pg J/K=scrl ,=up .=in Ret=open");
    a_vtab(23);
    cout_str("[X]ec [E]dit [F]new [R][D][N] [Q]uit");
}

/* -------------------------------------------------------------------
 * Resume note ("LASTRUN"). When a program is run from the
 * browser we drop a tiny note on the boot volume recording the program's
 * directory + filename. The interpreter's :quit cold-reboots back to this
 * launcher, which wipes the RAM handshake ($0C00 staged source / $1B06
 * length) — so the note has to be disk state. On the next boot we read the
 * note, delete it (one-shot), and re-open the browser positioned on that
 * file. Body format (packed, each field length-prefixed):
 *   [Plen][prefix..][Nlen][name..][origin][curLo][curHi]
 * The prefix keeps GET_PREFIX's trailing '/', which SET_PREFIX accepts. The
 * origin + cursor bytes reopen the editor where Ctrl-R left it.
 * ------------------------------------------------------------------- */

static uint8_t g_resume_name[16];   /* filename parsed from the note */
static uint8_t g_resume_editor;     /* note origin: 1 = reopen in the editor */

/* Build g_notepath = "/VOL/<tag>" (a 7-char note name, "LASTRUN" or the
 * harness's "TESTRUN"), where VOL is the first path component (volume) of
 * `prefix`. The note lives on the SAME disk as the file it points at — which
 * for the user's programs is the data disk (drive 2), the one with free space
 * (the boot disk is packed full, so a CREATE there fails). */
static void note_build_path(const uint8_t *prefix, const char *tag) {
    uint8_t i, k = 0, slashes = 0;
    for (i = 1; i <= prefix[0]; i++) {
        g_notepath[1 + k++] = prefix[i];
        if (prefix[i] == '/' && ++slashes == 2) break;   /* through "/VOL/" */
    }
    for (i = 0; i < 7; i++) g_notepath[1 + k++] = (uint8_t)tag[i];  /* 7-char tag */
    g_notepath[0] = k;
}

/* Serialize the just-run program's directory (g_prefix) + filename (g_path)
 * into g_note and WRITE it as LASTRUN on that program's own volume. Best
 * effort: a write-protected or full disk simply loses the resume. */
static void note_save(void) {
    uint8_t p = g_prefix[0], n = g_path[0], i, k = 0;
    if (p < 2) return;                        /* need a "/VOL/" to host the note */
    g_note[k++] = p;
    for (i = 1; i <= p; i++) g_note[k++] = g_prefix[i];
    g_note[k++] = n;
    for (i = 1; i <= n; i++) g_note[k++] = g_path[i];
    g_note[k++] = g_run_from_editor;          /* origin: reopen editor vs browser */
    g_note[k++] = (uint8_t)(g_cursor & 0xFF); /* editor cursor offset, little-endian */
    g_note[k++] = (uint8_t)(g_cursor >> 8);
    g_note_len = k;

    note_build_path(g_prefix, "LASTRUN");     /* note on the file's own volume */
    g_open_pathname = g_notepath;
    a_mli_destroy_note();                     /* clear any stale note (ignore) */
    if (a_mli_create_note() != 0) return;
    if (a_mli_open() != 0) return;            /* sets g_open_refnum, uses IOBUF */
    a_mli_write_note();
    a_mli_close();
}

/* Parse g_note (read from disk, `got` bytes) into g_path (= saved prefix,
 * ready for SET_PREFIX) and g_resume_name (= filename). Returns 1 if the
 * note is well-formed. A longer file (stale tail from a prior note) is
 * harmless: the embedded lengths bound the parse, not the file size. */
static uint8_t note_parse(uint16_t got) {
    uint8_t p, n, i, k = 0;
    if (got < 2) return 0;
    p = g_note[k++];
    if (p == 0 || p > 64 || (uint16_t)(1 + p + 1) > got) return 0;
    for (i = 0; i < p; i++) g_path[1 + i] = g_note[k++];
    g_path[0] = p;
    n = g_note[k++];
    if (n == 0 || n > 15 || (uint16_t)(k + n) > got) return 0;
    for (i = 0; i < n; i++) g_resume_name[1 + i] = g_note[k++];
    g_resume_name[0] = n;
    /* Optional trailing origin byte; older notes omit it -> 0. */
    g_resume_editor = (k < got) ? g_note[k] : 0;
    /* Optional trailing cursor offset (little-endian); -> 0 (top). */
    g_cursor = ((uint16_t)(k + 2) < got)
                   ? (uint16_t)(g_note[k + 1] | (g_note[k + 2] << 8))
                   : 0;
    return 1;
}

/* Defined far below (near the other chain_* helpers) — note_consume probes for
 * a TESTRUN note in the same online-volume scan and chains here if found. */
static void chain_testrun(void);

/* Read + consume the LASTRUN note. Returns 1 (having SET_PREFIXed into the
 * saved directory, with the filename left in g_resume_name) if a usable note
 * pointing at a program that STILL EXISTS was present; 0 otherwise (-> main
 * menu). The note is destroyed whenever it is found — so the cache link is
 * removed on every consume and the resume fires at most once — including when
 * the saved directory or file has since been deleted/renamed: those simply
 * fall through to the menu rather than reopening the browser on a stale spot. */
static uint8_t note_consume(void) {
    uint8_t v, j, k;
    uint16_t got;
    /* The note now lives on whichever disk the last file was on (usually the
     * data disk), so scan every online volume for a "/VOL/LASTRUN". g_path
     * doubles as the per-volume "/VOL/" prefix fed to note_build_path. */
    for (v = 0; v < 16; v++) g_online[(uint16_t)v * 16] = 0;
    a_mli_online();
    for (v = 0; v < 16; v++) {
        const uint8_t *rec = g_online + (uint16_t)v * 16;
        uint8_t nlen = (uint8_t)(rec[0] & 0x0F);
        if (nlen < 1 || nlen > 15) continue;          /* empty / error slot */
        k = 0;
        g_path[1 + k++] = '/';
        for (j = 0; j < nlen; j++) g_path[1 + k++] = rec[1 + j];
        g_path[1 + k++] = '/';
        g_path[0] = k;
        /* A test sweep in progress (TESTRUN note, design doc 018) takes
         * priority — hand straight to TESTRUN.SYSTEM, which runs the next test.
         * Folded into this scan so it costs no second ON_LINE pass. */
        note_build_path(g_path, "TESTRUN");
        g_open_pathname = g_notepath;
        if (a_mli_open() == 0) { a_mli_close(); chain_testrun(); }  /* chains if present */
        note_build_path(g_path, "LASTRUN");           /* g_notepath = /VOL/LASTRUN */
        g_open_pathname = g_notepath;
        if (a_mli_open() != 0) continue;              /* no note on this volume */
        got = a_mli_read_note();
        a_mli_close();
        a_mli_destroy_note();                         /* one-shot: remove it */
        if (!note_parse(got)) continue;               /* corrupt -> next volume */
        if (a_mli_set_prefix() != 0) continue;        /* saved dir gone */
        /* Confirm the saved program still exists (deleted/renamed -> skip). */
        g_open_pathname = g_resume_name;
        if (a_mli_open() != 0) continue;              /* file gone */
        a_mli_close();
        return 1;
    }
    return 0;
}

/* Does a current-pane row name `name` (length-prefixed)? */
static uint8_t names_equal_row(const uint8_t *row, const uint8_t *name) {
    uint8_t rn = (uint8_t)(row[0] & 0x0F), i;
    if (rn != name[0]) return 0;
    for (i = 0; i < rn; i++) if (row[1 + i] != name[1 + i]) return 0;
    return 1;
}

/* Position the current-pane highlight on the row named `name`, scrolling it
 * into view. Falls back to row 0 if the file is no longer there. */
static void select_row_by_name(const uint8_t *name) {
    uint8_t i;
    s_sel = 0; s_cur_top = 0;
    for (i = 0; i < s_cur_count; i++) {
        if (names_equal_row(CUR_TAB + (uint16_t)i * ROW_BYTES, name)) {
            s_sel = i;
            break;
        }
    }
    if (s_sel >= (uint8_t)(s_cur_top + PAGE_ROWS))
        s_cur_top = (uint8_t)(s_sel - (PAGE_ROWS - 1));
}

#ifdef LITE_IIE
/* Read a volume's free + total block counts from device `unit` (a ProDOS
 * unit_num, DSSS0000). Reads the volume-directory key block (block 2) for
 * total_blocks and the bitmap pointer, then sums the free bits across the
 * allocation bitmap block(s) — a set bit means the block is free. ProDOS has
 * no "blocks free" MLI call, so this mirrors what BASIC.SYSTEM's CATALOG does.
 * Returns 1 with the out-params filled, or 0 if a device read failed
 * (e.g. /RAM has no real bitmap) — the picker then shows "(no info)". */
static uint8_t vol_blocks(uint8_t unit, uint16_t *out_free, uint16_t *out_total) {
    uint16_t total, bmptr, remaining, blk, freeb = 0;

    g_rb_unit  = unit;
    g_rb_block = 2;                 /* volume directory key block */
    if (a_mli_read_block()) return 0;
    /* The volume-directory header begins at block offset 4; total_blocks is at
     * header +$25 (block +$29) and bit_map_pointer at header +$27 (block +$27). */
    total = (uint16_t)(DIRBLK[0x29] | (DIRBLK[0x2A] << 8));
    bmptr = (uint16_t)(DIRBLK[0x27] | (DIRBLK[0x28] << 8));
    if (total == 0) return 0;

    /* Each bitmap block holds 4096 bits (512 bytes). Trailing bits past the
     * volume's block count are kept 0 (used) by ProDOS, so a whole-byte popcount
     * never over-counts. */
    remaining = total;
    blk = bmptr;
    while (remaining) {
        uint16_t bits   = remaining > 4096 ? 4096 : remaining;
        uint16_t nbytes = (uint16_t)((bits + 7) >> 3);
        uint16_t i;
        g_rb_unit  = unit;
        g_rb_block = blk;
        if (a_mli_read_block()) return 0;
        for (i = 0; i < nbytes; i++) {
            uint8_t b = DIRBLK[i];
            while (b) { freeb = (uint16_t)(freeb + (b & 1)); b >>= 1; }
        }
        remaining = (uint16_t)(remaining - bits);
        blk++;
    }
    *out_free  = freeb;
    *out_total = total;
    return 1;
}
#endif /* LITE_IIE */

/* List the online ProDOS volumes (all drives) and let the user pick one to
 * switch the browser into. This is the only way to reach a second disk — e.g.
 * the data disk in slot 6 drive 2 — because ProDOS has no parent directory
 * above a volume root, so the up-a-directory key dead-ends there. Returns 1
 * after SET_PREFIXing into "/VOL/", 0 on cancel (Q/Esc) or if there is nothing
 * to switch to. Reached from the up-directory key when already at a volume
 * root. */
static uint8_t volume_picker(void) {
    static uint8_t  idx[14];     /* g_online record numbers that hold a volume */
#ifdef LITE_IIE
    static uint16_t vfree[14];   /* free blocks per listed volume               */
    static uint16_t vtot[14];    /* total blocks per listed volume              */
    static uint8_t  vok[14];     /* 1 if vfree/vtot were read successfully       */
#endif
    uint8_t count = 0;
    uint8_t sel = 0;
    uint8_t i, j, key;

    /* ON_LINE writes only the active devices' records; clear the record headers
     * first so any slot it skips reads as empty (length 0) rather than stale. */
    for (i = 0; i < 16; i++) g_online[(uint16_t)i * 16] = 0;
    a_mli_online();
    for (i = 0; i < 16 && count < 14; i++) {
        uint8_t nlen = (uint8_t)(g_online[(uint16_t)i * 16] & 0x0F);
        if (nlen >= 1 && nlen <= 15) idx[count++] = i;   /* skip empty slots */
    }
    if (count == 0) return 0;

#ifdef LITE_IIE
    /* Read each volume's free/total space once, here — not in the draw loop, so
     * arrow-key navigation never re-hits disk. The high nibble of the ON_LINE
     * record header is the device's unit_num. */
    for (i = 0; i < count; i++) {
        uint8_t unit = (uint8_t)(g_online[(uint16_t)idx[i] * 16] & 0xF0);
        vok[i] = vol_blocks(unit, &vfree[i], &vtot[i]);
    }
#endif

    {
#ifdef LITE_IIE
      /* Screen row of the first volume. The two-line column header is pinned to
       * the top (rows 0-1); the volume rows start a couple of rows below it so
       * the titles and the list are visually separated. Marker-move a_vtab()
       * and prompt_row are all derived from this, so the whole list shifts by
       * changing it. (//e only: the II+ launcher drops the disk-space readout to
       * protect its tight BSS budget — see docs/contributing/ROADMAP.md.) */
      const uint8_t list_top = 4;
#else
      /* Screen row of the first volume: a one-line title at row 0, a blank row,
       * then the list from row 2. (The II+ launcher omits the per-volume
       * free/total readout — see docs/contributing/ROADMAP.md.) Marker-move a_vtab() and
       * prompt_row derive from list_top. */
      const uint8_t list_top = 2;
#endif
      uint8_t draw_full = 1;
      /* Keyboard shortcuts pinned to the bottom row, matching the main menu's
       * prompt row (a_vtab(22)) — clear of the list, which tops out at row 17/15
       * (list_top + 14 volumes). Row 22 leaves row 23 free so nothing scrolls. */
      const uint8_t prompt_row = 22;
      for (;;) {
        const uint8_t *rec;
        if (draw_full) {
            uint8_t r;
            a_home();
#ifdef LITE_IIE
            /* Two-line header, each label sitting directly over its column: the
             * unit (KB / blocks) on top, "free/total" beneath, above the name /
             * KB / blocks columns the rows print. The KB and blocks "free/total"
             * are placed so each '/' lines up with the data '/' in its column. */
            cout_str("Disks / volumes     KB       blocks\r");
            cout_str("                free/total free/total\r");
            for (r = 2; r < list_top; r++) a_cout('\r'); /* gap below 2-line header */
#else
            cout_str("Disks / volumes\r");
            for (r = 1; r < list_top; r++) a_cout('\r'); /* gap below 1-line header */
#endif
            for (i = 0; i < count; i++) {
                uint8_t nn;
                rec = g_online + (uint16_t)idx[i] * 16;
                nn = (uint8_t)(rec[0] & 0x0F);
                cout_str(i == sel ? "> /" : "  /");
                for (j = 0; j < nn; j++) a_cout(rec[1 + j]);
#ifdef LITE_IIE
                for (j = nn; j < 12; j++) a_cout(' ');     /* pad to name column */
                if (vok[i]) {
                    /* 1 block = 0.5 KB, so KB = blocks >> 1. Free values are
                     * right-justified to fixed widths so the '/' in both the KB
                     * and blocks columns lines up across rows (sized for a
                     * 140K..1MB volume; larger ones just run the field wider). */
                    cout_str("  ");
                    cout_u16_rj((uint16_t)(vfree[i] >> 1), 3); a_cout('/');
                    cout_u16_rj((uint16_t)(vtot[i]  >> 1), 3); a_cout('K');
                    cout_str("  ");
                    cout_u16_rj(vfree[i], 4); a_cout('/');
                    cout_u16_rj(vtot[i],  4); cout_str("b");
                } else {
                    cout_str("  (no info)");
                }
#endif
                a_cout('\r');
            }
            a_vtab(prompt_row); cout_str("I/M=select  Ret=open  Q=back");
        }
        draw_full = 0;        /* navigation repaints only the moved marker */

        for (;;) { key = a_kbd(); if (key) break; }
        key &= 0x7F;
        if (key >= 'a' && key <= 'z') key = (uint8_t)(key - 0x20);
        if (key == 'Q' || key == 0x1B) return 0;
        if (key == 'M' || key == 0x0A) {
            if (sel + 1 < count) {
                a_vtab((uint8_t)(list_top + sel)); cout_str("  /");
                sel++;
                a_vtab((uint8_t)(list_top + sel)); cout_str("> /");
                a_vtab(prompt_row); cout_str("I/M=select  Ret=open  Q=back");
            }
            continue;
        }
        if (key == 'I' || key == 0x0B) {
            if (sel) {
                a_vtab((uint8_t)(list_top + sel)); cout_str("  /");
                sel--;
                a_vtab((uint8_t)(list_top + sel)); cout_str("> /");
                a_vtab(prompt_row); cout_str("I/M=select  Ret=open  Q=back");
            }
            continue;
        }
        if (key == 0x0D || key == 0x15 || key == '.') {
            uint8_t k = 0;
            rec = g_online + (uint16_t)idx[sel] * 16;
            g_path[1 + k++] = '/';
            for (j = 0; j < (uint8_t)(rec[0] & 0x0F); j++) g_path[1 + k++] = rec[1 + j];
            g_path[1 + k++] = '/';
            g_path[0] = k;
            if (a_mli_set_prefix() == 0) return 1;
            action_err("OPEN VOL", 0);   /* couldn't switch -> redraw the list */
            draw_full = 1;               /* action_err cleared the screen */
        }
      }
    }
}

/* Set in main() when COMPILER.SYSTEM is on the booted volume — a
 * Family B compiler disk. In that mode a "run" stages the source PATH for the
 * Compiler (not the source content for an interpreter). 0 on the REPL disks. */
static uint8_t g_familyb;

/* Family B: length-prefixed ABSOLUTE source path handed to the Compiler at
 * EDIT_PATH_ADDR. Absolute (not prefix-relative) because the chain restores
 * the boot prefix before the Compiler runs, but the source may live in a
 * browsed subdir / on drive 2 — MLI resolves an absolute path regardless. */
static uint8_t g_edit_abs[80];

/* Copy a length-prefixed path to EDIT_PATH_ADDR for the Compiler's
 * source_path() to read after the chain. $0C00 survives the chain READ
 * ($2000+) and the COMPILER.SYSTEM OPEN (MLI I/O buffer is at $1C00). */
static void stage_path_for_compiler(const uint8_t *lp) {
    volatile uint8_t *dst = (volatile uint8_t *)EDIT_PATH_ADDR;
    uint8_t i;
    for (i = 0; i <= lp[0]; i++) dst[i] = lp[i];
}

/* 1 if the row's filename ends in ".SWB" (a compiled program). Such a file
 * runs on the Runner directly ([X]) instead of being compiled. */
static uint8_t row_is_swb(const uint8_t *row) {
    uint8_t nn = (uint8_t)(row[0] & 0x0F);
    const uint8_t *nm = row + 1;
    if (nn < 4) return 0;
    return (uint8_t)(nm[nn - 4] == '.' &&
                     (nm[nn - 3] == 'S' || nm[nn - 3] == 's') &&
                     (nm[nn - 2] == 'W' || nm[nn - 2] == 'w') &&
                     (nm[nn - 1] == 'B' || nm[nn - 1] == 'b'));
}

/* Build g_edit_abs = current prefix + a browser row's name (length-prefixed). */
static void fb_abs_from_row(const uint8_t *row) {
    uint8_t nn = (uint8_t)(row[0] & 0x0F);
    uint8_t pn, k = 0, i;
    a_mli_get_prefix();                       /* refresh g_prefix (/VOL/DIR/) */
    pn = g_prefix[0];
    for (i = 0; i < pn && k < (uint8_t)(sizeof g_edit_abs - 2); i++)
        g_edit_abs[1 + k++] = g_prefix[1 + i];
    for (i = 0; i < nn && k < (uint8_t)(sizeof g_edit_abs - 2); i++)
        g_edit_abs[1 + k++] = row[1 + i];
    g_edit_abs[0] = k;
}

/* Stage the highlighted file's bytes at $0C00 for the interpreter and drop
 * the LASTRUN resume note. Returns the MLI OPEN error (0 = staged ok, ready
 * to chain). Shared by every run path (RET / [L]ite / extras key).
 *
 * Family B: there is no interpreter to stage source INTO — hand the Compiler
 * the source's absolute path at EDIT_PATH_ADDR and report 0 staged bytes
 * (main chains COMPILER.SYSTEM, which reads the path, compiles, chains the
 * Runner). */
static uint8_t stage_file(const uint8_t *row, uint16_t *staged_len_out) {
    uint8_t err;
    g_run_from_editor = 0;       /* a browser run resumes back in the browser */
    row_to_path(row);
    if (g_familyb) {
        fb_abs_from_row(row);
        stage_path_for_compiler(g_edit_abs);
        *staged_len_out = 0;
        note_save();
        return 0;
    }
    g_open_pathname = g_path;
    err = a_mli_open();
    if (err) return err;
    *staged_len_out = a_mli_read_startup();
    note_save();                 /* resume here after a :quit reboot */
    return 0;
}

/* The browser loop. Returns 1 when the user ran a file: it has staged the
 * source at $0C00, set *staged_len_out, and put the binary to chain in
 * *out_fname / *out_display. Returns 0 to go back to the main menu.
 *
 * Keys: I / M (or //e Up/Down) move the highlight (the II+ has no up/down
 *   arrows, hence letters); J (up) / K (down) scroll the preview pane.
 *   Directory in/out uses the left/right arrows OR , / . : `,` / left = up a
 *   directory, `.` / right (or RET) enters a folder, opens a .swift in the
 *   editor, or launches a .SYSTEM file. [X] eXecutes the selected .swift with
 *   this disk's sole interpreter (one per REPL disk); [E] edits, [F]
 *   makes a new file, [R]/[D]/[N] rename/delete/new-folder. Esc / [Q] -> menu.
 *
 * When the highlight rests on a TXT (`.swift`) file for ~1.5 s, its head
 * previews in the bottom pane (load_preview / draw_preview). `preselect`
 * (length-prefixed, or NULL/empty) highlights a starting file — used by the
 * resume path to reopen on the last-run program. */

/* ------------------------------------------------------------------
 * Slice 2: 80-column mode (//e disk only; see g_width80). The
 * launcher's text already routes through COUT ($FDED -> JMP (CSW)), so hooking
 * the //e 80-col firmware makes every a_cout land in 80 columns; a_home/a_vtab
 * (asm) gained 80-col arms. Mirrors src/platform/apple2/screen.c's proven
 * The //e sequence. The whole feature is gated to LITE_IIE — the II+ disk
 * has no 80-col path. On the II+ disk g_width80 stays 0.
 * ------------------------------------------------------------------ */
#ifdef LITE_IIE
/* //e built-in 80-col card: ProDOS MACHID ($BF98) bit 4 (a card-presence
 * probe, not the untrustworthy $FBB3 model byte). */
static uint8_t has_80col(void) {
    return (uint8_t)((*(volatile uint8_t *)0xBF98) & 0x10);
}
static void width80_on(void) {
    /* Bank motherboard ROM in BEFORE entering the firmware. The //e 80-col
     * firmware ($C300 + its $C800 body) JSRs monitor ROM routines at $F800+; if
     * the language card is banked to read RAM, those addresses read LC RAM —
     * which is where ProDOS keeps its MLI body — so the firmware executes (and
     * corrupts) ProDOS code instead of ROM, and every later MLI call returns $01
     * "bad call". The launcher normally runs LC=ROM, but the in-process editor's
     * cc65 file I/O leaves LC=RAM, and its Ctrl-W width toggle calls in here with
     * that state: the result was a garbled file listing when Ctrl-Q returned to
     * the browser (refresh_panes' GET_PREFIX failed, so reload_panes kept the
     * editor-clobbered pane tables). Mirrors editor.c's ed_rebank_rom; harmless
     * when LC is already ROM (the boot path). */
    __asm__("bit $C082");                  /* LC -> read motherboard ROM */
    __asm__("jsr $C300");                  /* enable //e 80-col firmware + hook CSW */
    g_width80 = 1;
}
static void width80_off(void) {
    *(volatile uint8_t *)0xC00C = 0;       /* 80COL off: 40-col video */
    *(volatile uint8_t *)0xC000 = 0;       /* 80STORE off */
    *(volatile uint8_t *)0x0021 = 40;      /* WNDWDTH = 40 (firmware widened it) */
    g_width80 = 0;
}

/* ---- 80-col preference persistence -------------------------------------
 * A 1-byte file "/BOOTVOL/SWIFT80" remembers the confirmed width across
 * reboots (so a program run + :quit reboot does NOT re-prompt). Reuses the
 * LASTRUN note MLI verbs (g_notepath / g_open_pathname / g_note). The file is
 * hidden from the browser (add_entry). Best-effort: a write-protected/full
 * boot disk just can't save, so the opt-in prompt re-appears next boot. */
#define PREF_UNSET 0xFF
#define PREF_40    0x00
#define PREF_80    0x01

/* g_notepath = "/BOOTVOL/SWIFT80" (volume of g_boot_prefix). */
static void pref_build_path(void) {
    static const char tag[] = "SWIFT80";
    uint8_t i, k = 0, slashes = 0;
    for (i = 1; i <= g_boot_prefix[0]; i++) {
        g_notepath[1 + k++] = g_boot_prefix[i];
        if (g_boot_prefix[i] == '/' && ++slashes == 2) break;   /* through "/VOL/" */
    }
    for (i = 0; i < 7; i++) g_notepath[1 + k++] = (uint8_t)tag[i];
    g_notepath[0] = k;
    g_open_pathname = g_notepath;
}

/* Saved width (PREF_40 / PREF_80), or PREF_UNSET if there is no pref file. */
static uint8_t pref_read(void) {
    uint16_t got;
    if (g_boot_prefix[0] < 2) return PREF_UNSET;
    pref_build_path();
    if (a_mli_open() != 0) return PREF_UNSET;            /* no pref file yet */
    got = a_mli_read_note();                             /* 1 byte -> g_note */
    a_mli_close();
    if (got < 1) return PREF_UNSET;
    return (g_note[0] == PREF_80) ? PREF_80 : PREF_40;
}

/* Persist the width (best-effort; a full/locked boot disk simply loses it). */
static void pref_save(uint8_t v) {
    if (g_boot_prefix[0] < 2) return;
    pref_build_path();
    g_note[0] = v;
    g_note_len = 1;
    a_mli_destroy_note();                                /* clear any stale (ignore) */
    if (a_mli_create_note() != 0) return;
    if (a_mli_open() != 0) return;
    a_mli_write_note();
    a_mli_close();
}

/* Enter 80-col, then a COUNTDOWN confirmation: any key keeps 80, the timeout
 * reverts to 40. Returns 1 if kept, 0 if reverted. The static text is drawn
 * ONCE and only the countdown line is rewritten each second (a_vtab to its
 * row), so the screen doesn't flicker. Because the choice is persisted, this
 * guards against saving an 80-col the display can't actually show. */
static uint8_t width80_confirm(void) {
    uint8_t sec;
    uint16_t t;
    width80_on();
    a_home();                                           /* 80-col clear via firmware */
    cout_str("SwiftII - 80 columns on\r\r");
    cout_str("Press any key to keep 80 columns.");      /* leaves cursor on row 2 */
    for (sec = 10; sec; sec--) {
        a_vtab(4);                                      /* only the countdown line */
        cout_str("Reverting to 40 in ");
        cout_u16(sec);
        cout_str(" ...  ");                             /* trailing spaces clear 10->9 */
        for (t = 0; t < 20000u; t++)                    /* ~1 s; mirrors DWELL_TICKS */
            if (a_kbd()) return 1;
    }
    width80_off();
    return 0;
}

/* VisiCalc-style 80-column opt-in. A 40-col default works on every Apple II, so
 * we only ASK on the FIRST boot with a card and no saved preference; the choice
 * is then saved (pref_save) and applied silently on later boots — so a program
 * run + reboot does NOT re-prompt. Entering 80-col always goes through the
 * countdown confirm, and only a confirmed 80 is saved, so a garbled 80-col
 * never persists. */
static void width80_startup(void) {
    uint8_t pref = pref_read();
    uint8_t key;
    if (pref == PREF_80) {
        if (has_80col()) width80_on();             /* remembered + confirmed; silent */
        return;                                    /* (card gone -> 40, keep the pref) */
    }
    if (pref == PREF_40) return;                   /* remembered 40; silent */
    /* Unset = first boot. */
    if (!has_80col()) return;                      /* no card -> silent 40 (don't save) */
    a_home();
    cout_str("SwiftII v");
    cout_str(SWIFTII_VERSION);
    cout_str("  ");
    cout_str(BUILD_NAME);
    a_cout('\r');
    cout_str("(C) Yeo Kheng Meng ");
    cout_year();
    cout_str("\r\r");
    cout_str("80-column card detected.\r\r");
    cout_str("Use 80 columns? [Y]=Yes  [N]=No > ");
    do { key = a_kbd(); } while (!key);
    a_cout('\r');
    if ((key & 0x5F) == 'Y') pref_save(width80_confirm() ? PREF_80 : PREF_40);
    else                     pref_save(PREF_40);
}

/* Sticky [W] toggle (menu + browser), and re-saves the preference. 80 -> 40 is
 * instant; 40 -> 80 goes through the countdown confirm (so a garbled toggle
 * neither sticks nor gets saved as 80). Works as a manual switch into 80-col
 * even with no auto-detected card. The caller redraws. */
static void width80_toggle(void) {
    if (g_width80) { width80_off(); pref_save(PREF_40); return; }
    pref_save(width80_confirm() ? PREF_80 : PREF_40);
}

/* Editor's in-session width fallback (Ctrl-W): switch the text width WITHOUT the
 * menu's countdown / pref save. Called from editor.c (slice 3b), //e disk only. */
void ed_width(uint8_t on) {
    if (on) { if (!g_width80) width80_on(); }
    else    { if (g_width80)  width80_off(); }
}
#endif /* LITE_IIE — 80-column feature */

/* Run the editor in-process. It renders at the launcher's current width itself
 * (40-col to the text page, 80-col through the firmware, gated on g_width80), so
 * no temp width switch here. Re-bank-ROM-on-return is handled inside editor_main.
 * Returns the editor's code (EDITOR_QUIT / EDITOR_RUN). */
static int run_editor(const char *path, uint16_t cursor) {
    return editor_main(path, cursor);
}

/* Open the selected file in the editor IN-PROCESS (merge): build its
 * absolute path (current prefix + name) and call editor_main() via run_editor,
 * which returns here on Ctrl-Q (EDITOR_QUIT) or Ctrl-R (EDITOR_RUN) — the code
 * is returned to the caller. The path is built into a high-BSS static, NOT the
 * $0800-$1BFF window editor_main's gap buffer will overwrite. The caller
 * rebuilds + redraws the browser on a QUIT return (a save may have created or
 * grown a file). Both disks. Shared by RET and the [E] key. */
/* Absolute editor path (high BSS, survives the gap buffer's gapbuf_init).
 * Shared by open_in_editor and the resume-in-editor path so the two don't each
 * carry an 80-byte static. */
static char ed_path[80];

static int open_in_editor(const uint8_t *row) {
    uint8_t nn = (uint8_t)(row[0] & 0x0F);
    uint8_t pn, k = 0, i;
    a_mli_get_prefix();                       /* refresh g_prefix */
    pn = g_prefix[0];
    for (i = 0; i < pn && k + 1u < sizeof ed_path; i++)
        ed_path[k++] = (char)g_prefix[1 + i]; /* /VOL/DIR/ */
    for (i = 0; i < nn && k + 1u < sizeof ed_path; i++)
        ed_path[k++] = (char)row[1 + i];      /* name      */
    ed_path[k] = '\0';
    return run_editor(ed_path, 0);   /* fresh open from the browser -> top */
}

/* Editor Ctrl-R (EDITOR_RUN): stage the editor's just-saved file at $0C00 and
 * fill the chain-out params, exactly as stage_file does for a browser row but
 * keyed off the editor's absolute path (which may be a brand-new or renamed
 * file with no directory row). Splits the path into directory + filename for
 * the LASTRUN resume note, opens it directly (MLI accepts an absolute path),
 * and runs it with this disk's sole interpreter (one per REPL disk).
 * Returns 1 when staged (file_browser should break to main()'s chain), 0 on
 * open error. */
static uint8_t run_abs_path(const char *abspath, uint16_t *staged_len_out,
                            const uint8_t *sole_fname, const char *sole_display,
                            const uint8_t **out_fname, const char **out_display) {
    static uint8_t openpath[65];
    uint8_t len = 0, i, slash = 0, n;
    g_run_from_editor = 1;       /* Ctrl-R from the editor -> resume in editor */
    g_cursor = editor_saved_cursor();        /* reopen the editor at this offset */
    while (abspath[len] && len < 64) { if (abspath[len] == '/') slash = len; len++; }
    openpath[0] = len;                                   /* len-prefixed, absolute */
    for (i = 0; i < len; i++) openpath[1 + i] = (uint8_t)abspath[i];
    if (g_familyb) {
        /* Family B: hand the Compiler the absolute path; no content staging. */
        stage_path_for_compiler(openpath);
        *staged_len_out = 0;
        /* still record dir/name for the LASTRUN resume + "Loading X" line. */
        g_prefix[0] = (uint8_t)(slash + 1);
        for (i = 0; i <= slash; i++) g_prefix[1 + i] = (uint8_t)abspath[i];
        n = (uint8_t)(len - (slash + 1));
        g_path[0] = n;
        for (i = 0; i < n; i++) g_path[1 + i] = (uint8_t)abspath[slash + 1 + i];
        note_save();
        *out_fname = sole_fname; *out_display = sole_display;
        return 1;
    }
    g_open_pathname = openpath;
    if (a_mli_open() != 0) return 0;                     /* unreadable -> stay in browser */
    *staged_len_out = a_mli_read_startup();
    /* directory prefix (incl. trailing '/') -> g_prefix; filename -> g_path,
     * the layout note_save serialises (and cout_path prints as "Loading X"). */
    g_prefix[0] = (uint8_t)(slash + 1);
    for (i = 0; i <= slash; i++) g_prefix[1 + i] = (uint8_t)abspath[i];
    n = (uint8_t)(len - (slash + 1));
    g_path[0] = n;
    for (i = 0; i < n; i++) g_path[1 + i] = (uint8_t)abspath[slash + 1 + i];
    note_save();
    *out_fname = sole_fname; *out_display = sole_display;
    return 1;
}

/* After the editor returns, act on its code: EDITOR_RUN stages the saved file
 * and asks file_browser to break to the chain (returns 1); anything else (a
 * plain quit, or a staging error) returns 0 so the browser keeps listing. */
static uint8_t editor_returned(int rc, uint16_t *staged_len_out,
                               const uint8_t *sole_fname, const char *sole_display,
                               const uint8_t **out_fname, const char **out_display) {
    if (rc != EDITOR_RUN) return 0;
    return run_abs_path(editor_saved_path(), staged_len_out,
                        sole_fname, sole_display, out_fname, out_display);
}

/* Is a length-prefixed file present on the current (boot) volume? OPEN + CLOSE,
 * best-effort. Used at startup to discover this REPL disk's SOLE interpreter
 * (launcher + ONE of SWIFTIIP/SWIFTSAT/SWIFTIIE/SWIFTAUX). */
static uint8_t file_on_disk(const uint8_t *name) {
    g_open_pathname = name;
    if (a_mli_open() != 0) return 0;
    a_mli_close();
    return 1;
}

/* PRODOS is the OS kernel, not a $2000 application — "launching" it would run
 * garbage. Block it by name. */
static const uint8_t fname_prodos[] = { 6, 'P','R','O','D','O','S' };

/* The standalone detection-diagnostic program (the Debug screen moved
 * out of the launcher into its own SYS binary so its code stops competing with
 * the launcher's tight BSS budget — see chain_debug). Chained from menu Debug. */
static const uint8_t fname_debug[] =
    { 12, 'D','E','B','U','G','.','S','Y','S','T','E','M' };

/* Launch the selected ProDOS system file (file_type $FF): open it in the
 * current directory and chain it. Most SYS files (the lite binary, the editor,
 * this launcher, a user's own .SYSTEM) are plain $2000 programs and take the
 * generic READ-into-$2000 + JMP path (a_install_and_chain). The packed extras
 * binaries (SWIFTSAT / SWIFTAUX — a 4-byte header + main + extras chunk, doc
 * 012) need their header-stripping chains, the same ones the menu uses, so
 * route them by name. Clears the source/edit handshake first so an interpreter
 * or editor launched this way starts clean. Never returns (on success). */
static void launch_sys(const uint8_t *row) {
    uint8_t err;
    if (names_equal_row(row, fname_prodos)) {
        clear_prompt_area();
        cout_str("\rCannot launch ProDOS itself - key");
        getkey();
        return;
    }
    *(uint16_t *)STAGED_LEN_ADDR = 0;      /* no staged Swift source */
    *(uint8_t  *)EDIT_PATH_ADDR = 0;       /* no editor path         */
    row_to_path(row);                      /* name -> g_path (rel. to prefix) */
    g_open_pathname = g_path;
    err = a_mli_open();
    if (err) { action_err("OPEN", err); return; } /* caller redraws the list */
    if (names_equal_row(row, fname_swiftsat))      a_install_and_chain_swiftsat();
    else if (names_equal_row(row, fname_swiftaux)) a_install_and_chain_swiftaux();
    else                                           a_install_and_chain();
    /* never returns */
}

static uint8_t file_browser(uint16_t *staged_len_out, const uint8_t *preselect,
                            const uint8_t *sole_fname,
                            const char *sole_display,
                            const uint8_t **out_fname,
                            const char **out_display) {
    uint8_t key, err;
    uint8_t *row;
    uint8_t bdraw_clear = 1;   /* 1 = full clear+redraw; 0 = no-clear (navigation) */
    uint8_t prev_only = 0;     /* 1 = redraw ONLY the preview pane (J/K scroll) */

#ifdef LITE_IIE
    /* //e: assert the alternate character set so the preview's lowercase
     * ($E0..$FF screen codes) render as lowercase glyphs (mirrors
     * platform_init in screen.c). The uppercase menus are unaffected. */
    *(volatile uint8_t *)0xC00F = 0;            /* SETALTCHAR */
#endif

    err = refresh_panes();
    if (err) {
        a_home();
        cout_str("Cannot read directory (err $");
        a_print_hex(err);
        cout_str(")\rPress a key");
        getkey();
        return 0;
    }
    if (preselect && preselect[0]) select_row_by_name(preselect);
    else { s_sel = 0; s_cur_top = 0; }
    prev_reset();

    for (;;) {
        /* Preview-pane-only redraw (J/K scroll): repaint just the bottom split,
         * not the whole screen. The panes/header/legend above are unchanged by a
         * preview scroll, so the slow full a_home + browser redraw is wasted. */
        if (prev_only) { prev_only = 0; draw_preview(); }
        else {
            draw_browser(bdraw_clear);
            bdraw_clear = 1;      /* default; navigation sets 0 just before its continue */
        }

        /* Wait for a key, but after ~1.5 s idle on a not-yet-previewed
         * selection, load + render the bottom preview pane (TXT files only).
         * Modeled on read_name's idle-poll; a_kbd is non-blocking. */
        {
            uint16_t t = 0;
            uint8_t kk;
            for (;;) {
                kk = a_kbd();
                if (kk) { key = (uint8_t)(kk & 0x7F); break; }
                if (!s_prev_done && ++t >= DWELL_TICKS) {
                    if (s_cur_count)
                        load_preview(CUR_TAB + (uint16_t)s_sel * ROW_BYTES);
                    else
                        s_prev_done = 1;
                    draw_preview();            /* onto the live (un-cleared) screen */
                }
            }
        }
        if (key >= 'a' && key <= 'z') key = (uint8_t)(key - 0x20);

        if (key == 'Q' || key == 0x1B) return 0;          /* Esc/Q -> menu */

        if (key == 'M' || key == 0x0A) {                  /* down (M / //e Down) */
            if (s_cur_count && s_sel + 1 < s_cur_count) {
                s_sel++;
                if (s_sel >= (uint8_t)(s_cur_top + PAGE_ROWS)) s_cur_top++;
                prev_reset();
                bdraw_clear = 0;          /* in-place redraw -> no flicker */
            }
            continue;
        }
        if (key == 'I' || key == 0x0B) {                  /* up (I / //e Up) */
            if (s_sel) {
                s_sel--;
                if (s_sel < s_cur_top) s_cur_top--;
                prev_reset();
                bdraw_clear = 0;          /* in-place redraw -> no flicker */
            }
            continue;
        }
        /* Page a near-window at a time (PAGE_ROWS-2), so ~2 entries from the
         * previous screen stay visible as context. */
        if (key == 0x16) {                                /* Ctrl-V = page list down */
            if (s_cur_count && s_sel + 1 < s_cur_count) {
                s_sel = (uint8_t)(s_sel + (PAGE_ROWS - 2)) < s_cur_count
                            ? (uint8_t)(s_sel + (PAGE_ROWS - 2))
                            : (uint8_t)(s_cur_count - 1);
                if (s_sel >= (uint8_t)(s_cur_top + PAGE_ROWS))
                    s_cur_top = (uint8_t)(s_sel - (PAGE_ROWS - 1));
                prev_reset();
                bdraw_clear = 0;          /* in-place redraw -> no flicker */
            }
            continue;
        }
        if (key == 0x14) {                                /* Ctrl-T = page list up */
            if (s_sel) {
                s_sel = s_sel >= (PAGE_ROWS - 2)
                            ? (uint8_t)(s_sel - (PAGE_ROWS - 2)) : 0;
                if (s_sel < s_cur_top) s_cur_top = s_sel;
                prev_reset();
                bdraw_clear = 0;          /* in-place redraw -> no flicker */
            }
            continue;
        }
        if (key == 'J') {                                 /* J = scroll preview up */
            if (s_prev_loaded) {
                if (s_prev_top)
                    s_prev_top = (s_prev_top >= PREVIEW_ROWS)
                                     ? (uint16_t)(s_prev_top - PREVIEW_ROWS) : 0;
                prev_only = 1;            /* repaint just the preview pane (fast) */
            }
            continue;
        }
        if (key == 'K') {                                 /* K = scroll preview down */
            if (s_prev_loaded) {
                uint16_t maxtop = (s_prev_lines > PREVIEW_ROWS)
                                      ? (uint16_t)(s_prev_lines - PREVIEW_ROWS) : 0;
                s_prev_top = (uint16_t)(s_prev_top + PREVIEW_ROWS);
                if (s_prev_top > maxtop) s_prev_top = maxtop;
                prev_only = 1;            /* repaint just the preview pane (fast) */
            }
            continue;
        }
        if (key == 0x08 || key == ',') {                  /* left / ',' = up a directory */
            uint8_t from[16], i;
            /* Remember the directory we are leaving so the highlight can land
             * on it in the parent pane (s_curname is the current dir's own
             * name; reload_panes overwrites it for the new prefix). */
            for (i = 0; i <= s_curname[0]; i++) from[i] = s_curname[i];
            if (exit_dir() != 0xFF) {
                reload_panes();
                select_row_by_name(from);   /* land on the dir we came from */
                prev_reset();
            } else if (volume_picker()) {   /* at a volume root -> pick a disk */
                s_sel = 0; s_cur_top = 0;
                reload_panes();
                prev_reset();
            }
            continue;
        }
        if (key == 'N') {                                 /* mkdir (new folder) */
            g_path[0] = 0;
            clear_prompt_area();
            if (read_name("New folder: ", g_path, 0) && g_path[0]) {
                err = a_mli_create_dir();
                if (err) action_err("MKDIR", err);
            }
            reload_panes();
            prev_reset();
            continue;
        }

        if (!s_cur_count) continue;                       /* rest need a row */
        row = CUR_TAB + (uint16_t)s_sel * ROW_BYTES;

        /* RET / right-arrow / '.' = OPEN: enter a folder, open a .swift in the
         * editor in-process, or launch a .SYSTEM file. The editor may return
         * EDITOR_RUN (Ctrl-R), which stages the saved file + breaks to the
         * chain; on Ctrl-Q it returns here and we rebuild + redraw. */
        if (key == 0x0D || key == 0x15 || key == '.') {
            if (row[0] & 0x80) {                          /* a directory -> enter */
                if (enter_dir(row) == 0) {
                    s_sel = 0; s_cur_top = 0; reload_panes(); prev_reset();
                }
                continue;
            }
            if (row[16] == 0xFF) { launch_sys(row); continue; }   /* a SYS file */
            if (editor_returned(open_in_editor(row), staged_len_out,
                                sole_fname, sole_display, out_fname, out_display))
                return 1;
            reload_panes();
            prev_reset();
            continue;
        }
        /* [X] = eXecute the selected .swift with this disk's sole interpreter:
         * stage the source at $0C00 and break to main()'s chain. */
        if (key == 'X') {
            if (row[0] & 0x80) continue;                  /* folders don't run */
            if (row[16] == 0xFF) {                        /* a SYS binary */
                clear_prompt_area();
                cout_str("\rNot a .swift file - key");
                getkey();
                continue;
            }
            err = stage_file(row, staged_len_out);
            if (err) { action_err("OPEN", err); continue; }
            /* A compiled .swb runs on the Runner directly; a .swift goes to
             * the Compiler (Family B) or this disk's interpreter (Family A).
             * stage_file already put the file's path/content where the chained
             * binary expects it. */
            if (g_familyb && row_is_swb(row)) {
                *out_fname = fname_runner; *out_display = "RUNNER.SYSTEM";
            } else {
                *out_fname = sole_fname; *out_display = sole_display;
            }
            return 1;
        }
        if (key == 'E') {                                 /* edit selected file */
            if (row[0] & 0x80) continue;                  /* not a folder */
            if (row[16] != 0xFF) {                        /* not a system binary */
                if (editor_returned(open_in_editor(row), staged_len_out,
                                    sole_fname, sole_display, out_fname, out_display))
                    return 1;
                reload_panes();
                prev_reset();
                continue;
            }
            clear_prompt_area();
            cout_str("\rCannot edit a system file - key");
            getkey();
            continue;
        }
        if (key == 'F') {                                 /* new (empty) file */
            /* Open the editor in-process with no path -> a scratch buffer;
             * Ctrl-S / Ctrl-R prompt SAVE AS: for the new name. Returns on
             * Ctrl-Q (back to browse) or Ctrl-R (stage the new file + chain). */
            if (editor_returned(run_editor((const char *)0, 0), staged_len_out,
                                sole_fname, sole_display, out_fname, out_display))
                return 1;
            reload_panes();
            prev_reset();
            continue;
        }
#ifdef LITE_IIE
        if (key == 'W') {                                 /* toggle text width 40/80 (//e) */
            width80_toggle();
            reload_panes();
            prev_reset();
            continue;
        }
#endif
        if (key == 'R') {                                 /* rename */
            uint8_t i;
            row_to_path(row);                             /* old -> g_path */
            for (i = 0; i <= g_path[0]; i++) g_path2[i] = g_path[i]; /* prefill */
            clear_prompt_area();
            /* Edit the pre-populated name; Esc cancels, unchanged = no-op. */
            if (read_name("Rename to: ", g_path2, 1) &&
                g_path2[0] && !names_equal(g_path, g_path2)) {
                err = a_mli_rename();
                if (err) action_err("RENAME", err);
            }
            reload_panes();
            prev_reset();
            continue;
        }
        if (key == 'D') {                                 /* delete */
            uint8_t n, j;
            clear_prompt_area();
            a_cout('\r');
            cout_str("Delete ");
            n = (uint8_t)(row[0] & 0x0F);
            for (j = 0; j < n; j++) a_cout(row[1 + j]);
            cout_str("? Y/N ");
            key = getkey();
            if ((key & 0x5F) == 'Y') {
                row_to_path(row);
                err = a_mli_destroy();
                if (err) action_err("DELETE", err);
            }
            reload_panes();
            prev_reset();
            continue;
        }
        /* any other key: redraw */
    }
}

/* Which single interpreter THIS disk carries, detected in main() via
 * file_on_disk: 1 = the extras binary (SWIFTSAT/SWIFTAUX), 0 = lite
 * (SWIFTIIP/SWIFTIIE). The About page names that one interpreter (each REPL
 * disk has exactly one), not a lite-vs-extras choice. */
static uint8_t g_sole_extras;

/* The per-disk Help screen used to live here; it moved to a README.TXT file in
 * each disk's root (opened from the File selector / editor) to reclaim launcher
 * code space. The About screen points the user at it. */

/* About screen. ONE implementation shared by every build (II+, //e, Family B,
 * 40- and 80-col) so the level of detail is identical everywhere — the only
 * per-build differences are the one-line "This disk:" banner and the BUILD_NAME
 * tag, which identify the machine/disk by design. (The old 80-col variant
 * show_about_80 was removed; 80-col mode renders this same 40-col About.)
 *
 * Compact by design (trimmed to fund the editor fast paths): version, build
 * date+time, cc65 toolchain version, the disk banner, a README.TXT pointer,
 * copyright. The Swift-
 * language blurb and the per-disk feature rundown live only in each disk's
 * README.TXT, which this screen points the user at. Plain upper-case ASCII for
 * the pre-IIe character ROM. */
static void show_about(void) {
    a_home();
    cout_str("SwiftII v");
    cout_str(SWIFTII_VERSION);
    cout_str("  ");
    cout_str(BUILD_NAME);
    a_cout('\r');
    cout_str("Built ");
    cout_str(__DATE__);
    a_cout(' ');
    cout_str(__TIME__);
    a_cout('\r');
    cout_str("Compiled w/ cc65 v");
    cout_str(CC65_VER_STR);
    cout_str("\r\r");
    cout_str("This disk: ");
    cout_str(g_familyb ? FAMILYB_BANNER
                       : (g_sole_extras ? EXTRAS_BANNER : LITE_BANNER));
    cout_str("\r\r");
    cout_str("Help: open README.TXT.\r\r");
    cout_str("(C) Yeo Kheng Meng ");
    cout_year();
    a_cout('\r');
}

/* Detection diagnostic: chain the standalone DEBUG.SYSTEM. The screen that used
 * to be drawn here in-process (FBB3 / MACHID / Saturn slot readbacks / Videx
 * $C305+$C307) moved into its own SYS binary so its ~280 B of code+strings no
 * longer sit in the launcher's near-zero BSS headroom — that room funds the
 * II+ editor's typing fast path. The values the boot probe DERIVED that the
 * diagnostic shows — the slot-0 and slot-N Saturn $D000 readbacks, plus the
 * definitive Saturn-slot verdict (g_saturn_slot) — are parked into low RAM
 * ($1B80, below the $2000 load region so it survives the chain READ);
 * DEBUG.SYSTEM re-reads the directly-available ones itself (FBB3
 * once it banks ROM, MACHID, and the Videx $C305/$C307). Then chain it, which
 * displays everything, waits for a key, and chains back to SWIFTII.SYSTEM. On
 * //e drop to 40-col first (DEBUG.SYSTEM is a 40-col tool); the launcher
 * re-enables 80-col from its pref when DEBUG chains back. Returns only if
 * DEBUG.SYSTEM can't be opened (then the caller re-shows the menu). */
#define DEBUG_PARK ((volatile uint8_t *)0x1B80u)
static void chain_debug(void) {
    DEBUG_PARK[0] = g_slot0_readback;
    DEBUG_PARK[1] = g_slot_readback;
    DEBUG_PARK[2] = g_saturn_slot;      /* definitive Saturn verdict: 0..7 / $FF */
#ifdef LITE_IIE
    if (g_width80) { width80_off(); a_home(); }
#endif
    g_open_pathname = fname_debug;
    if (a_mli_open() == 0) a_install_and_chain();   /* never returns */
}

/* Chain TESTRUN.SYSTEM (design doc 018) from its fixed path on the data disk
 * (/SWIFTII.DATA/TESTRUN.SYSTEM). The interpreters live on the BOOT volume, so
 * restore the boot prefix first — the harness inherits it; the absolute open
 * ignores it. Returns only if the data disk isn't mounted. */
static void chain_testrun(void) {
    uint8_t i;
#ifdef LITE_IIE
    if (g_width80) { width80_off(); a_home(); }
#endif
    if (g_boot_prefix[0]) {
        for (i = 0; i <= g_boot_prefix[0]; i++) g_path[i] = g_boot_prefix[i];
        a_mli_set_prefix();
    }
    g_open_pathname = fname_testrun;                 /* "/SWIFTII.DATA/TESTRUN.SYSTEM" */
    if (a_mli_open() == 0) a_install_and_chain();    /* never returns */
    a_home();
    cout_str("No data disk (drive 2) with the\rtests was found.  Press a key > ");
    (void)getkey();
}

/* The four menu options sit at screen rows 3-6 (after the 3-line banner). */
#define MENU_OPT_ROW 3
#define MENU_OPT_N   5
#define MENU_PROMPT  "I/M move  Ret/-> or 1-5 select > "
/* Family B has no REPL, so its menu drops option 1 -> 3 options, prompt 1-3. */
#define MENU_PROMPT_FB "I/M move  Ret/-> or 1-4 select > "
static uint8_t g_menu_n = MENU_OPT_N;   /* set to 3 on a Family B disk in main */

/* Repaint ONLY the option markers (the "> " highlight) for `sel`, then leave
 * the cursor back on the select prompt. Moving the highlight this way avoids
 * the full a_home() clear + COUT redraw, which visibly flickers the whole
 * screen on a 1 MHz Apple II. */
static void menu_markers(uint8_t sel) {
    uint8_t i;
    for (i = 0; i < g_menu_n; i++) {
        a_vtab((uint8_t)(MENU_OPT_ROW + i));
        cout_str(sel == i ? "> " : "  ");
    }
    a_vtab(22); cout_str(g_familyb ? MENU_PROMPT_FB : MENU_PROMPT);
}

/* -------------------------------------------------------------------
 * main — never returns. Either chains into the interpreter at $2000
 * via the ZP bouncer or halts on MLI error.
 * ------------------------------------------------------------------- */

int main(void) {
    const uint8_t *chosen_fname;
    const char *chosen_display;
    uint8_t key;
    uint16_t staged_len = 0;       /* bytes staged at $0C00 by the File selector */
    const uint8_t *extras_fname;   /* this disk's possible extras binary name */
    const char *extras_display;
    const uint8_t *sole_fname;     /* the ONE interpreter on this disk (run target) */
    const char *sole_display;
    const char *sole_banner;       /* its front-page banner (SwiftII ][+ / Saturn / …) */
    uint8_t resumed = 0;           /* resume note took us straight to a run */
    uint8_t menu_sel;              /* highlighted main-menu option (0..3) */

    /* cc65's apple2 crt0 leaves the language card in bank-1-RAM-read /
     * write-protect mode after copying its LC segment. Our a_cout /
     * a_home / a_wait helpers JSR into ROM at $FC58 / $FDED / $FCA8,
     * which would otherwise execute MLI body bytes in bank-1 RAM and
     * crash on the first $00 byte (BRK). Flip the LC to ROM-read here
     * so all subsequent ROM calls hit real ROM. probe_saturn restores
     * this mode on exit; the chain code does its own pre-MLI bank-1
     * switch just before invoking MLI. See docs/contributing/LESSONS.md 2026-05-26. */
    __asm__("bit $C082");

    /* Brief splash while the (sometimes slow on real hardware) hardware
     * probes run; the menu redraws over it below. */
    a_home();
    cout_str("SwiftII - detecting hardware...\r");

    /* --- Detection (writes to g_saturn_slot / g_aux_found) --- */
    a_probe_saturn();
    a_probe_aux();

    /* Park the detected Saturn slot at SX_SAT_SLOT ($1B04) so it survives the
     * chain (it's below the $2000+ READ). The Tier-2 (Saturn) Family B Compiler
     * + Runner read it to set up their bytecode store; harmless otherwise — the
     * Tier-1 baseline binaries and the lite/editor never read it, and SWIFTSAT's
     * own chain restamps the same value. Stashed once here, before any chain. */
    *(volatile unsigned char *)0x1B04u = g_saturn_slot;

    /* Remember the boot prefix (the volume the interpreters live on). The
     * file browser can SET_PREFIX into subdirectories, so we restore this
     * before chaining or the interpreter OPEN would resolve against the
     * wrong directory and fail. */
    if (a_mli_get_prefix() == 0) {
        uint8_t i;
        for (i = 0; i <= g_prefix[0]; i++) g_boot_prefix[i] = g_prefix[i];
    } else {
        g_boot_prefix[0] = 0;
    }

    /* --- This REPL disk's SOLE interpreter (launcher + ONE binary).
     * Detect which is on the booted volume — prefer the extras binary if
     * present, else lite — and use it for the banner + every run path (REPL +
     * File selector). The disk choice, not a HW probe, picks the tier. --- */
#ifdef LITE_IIE
    extras_fname   = fname_swiftaux;
    extras_display = "SWIFTAUX.SYSTEM";
#else
    extras_fname   = fname_swiftsat;
    extras_display = "SWIFTSAT.SYSTEM";
#endif
    if (file_on_disk(extras_fname)) {
        sole_fname = extras_fname; sole_display = extras_display; sole_banner = EXTRAS_BANNER;
        g_sole_extras = 1;
    } else if (file_on_disk(fname_compiler)) {
        /* Family B: no REPL interpreter — the Compiler is the run
         * target (it chains the Runner itself). */
        sole_fname = fname_compiler; sole_display = "COMPILER.SYSTEM";
        sole_banner = FAMILYB_BANNER; g_familyb = 1; g_sole_extras = 0;
        g_menu_n = 4;                 /* drop the REPL option (no interpreter) */
    } else {
        sole_fname = fname_lite;   sole_display = LITE_DISPLAY;   sole_banner = LITE_BANNER;
        g_sole_extras = 0;
    }
    /* Clear any stale staged path so a run that didn't set one (Family B
     * option 1) leaves the Compiler on its DEFAULT_SRC_PATH fallback. */
    *(uint8_t *)EDIT_PATH_ADDR = 0;

    /* --- 80-column opt-in (VisiCalc-style, //e disk only): probe the card,
     * ask, and set the sticky g_width80 before any menu/browser draw, so the
     * chosen width holds across the whole session (and into the editor). The
     * II+ disk has no 80-col path, so this is gated out there. --- */
#ifdef LITE_IIE
    width80_startup();
#endif

    /* --- resume. If a previous file-run left a LASTRUN note,
     * drop straight into the browser positioned on that file (the menu is
     * skipped). Running from there picks a REPL and launches as usual;
     * quitting the browser ([Q]) falls through to the normal menu. The
     * note has already been consumed (one-shot) by note_consume(). --- */
    /* note_consume's online-volume scan also probes for a TESTRUN note and
     * chains TESTRUN.SYSTEM if a test sweep is in progress (design doc 018),
     * so the resume below stays focused on the browser/editor LASTRUN case. */
    if (note_consume()) {
        if (g_resume_editor) {
            /* The run came from the editor (Ctrl-R); reopen the editor on the
             * same file so edit -> run -> edit is a loop. Build its absolute
             * path = saved prefix (g_path, already SET_PREFIXed) + filename
             * into the shared editor-path buffer. */
            uint8_t k = 0, i;
            for (i = 1; i <= g_path[0] && k + 1u < sizeof ed_path; i++)
                ed_path[k++] = (char)g_path[i];
            for (i = 1; i <= g_resume_name[0] && k + 1u < sizeof ed_path; i++)
                ed_path[k++] = (char)g_resume_name[i];
            ed_path[k] = '\0';
            if (editor_returned(run_editor(ed_path, g_cursor), &staged_len,
                                sole_fname, sole_display,
                                &chosen_fname, &chosen_display))
                resumed = 1;
            /* editor quit -> fall through to the menu */
        } else if (file_browser(&staged_len, g_resume_name, sole_fname,
                                sole_display, &chosen_fname, &chosen_display)) {
            resumed = 1;
        }
    }

    /* --- Main menu. Re-shown after About/Debug, an invalid key, or a
     * cancelled sub-prompt; a final choice breaks out to the launch/chain code.
     * Skipped entirely when the resume path above already chose a run. The top
     * line shows THIS disk's interpreter banner (the build name of the disk).
     * The `>` highlight (default = REPL) moves with I/M / //e Up-Down and
     * Return activates it; the 1-5 number keys jump straight to an option. --- */
    menu_sel = 0;
    if (!resumed)
    for (;;) {
        a_home();
        cout_str(sole_banner);            /* e.g. "SwiftII ][+ Saturn" */
        cout_str("  v");
        cout_str(SWIFTII_VERSION);
        a_cout('\r');
        cout_str("(C) Yeo Kheng Meng ");
        cout_year();
        cout_str("\r\r");

        /* "Run tests" (chains TESTRUN.SYSTEM on the data disk, design doc 018)
         * sits just above About. The position->action mapping below stays
         * linear: action 3 = Run tests, action 4 = About. It is reached only
         * via its menu option (digit / RET on the highlight) — there is no [T]
         * hot-key. */
        if (g_familyb) {
            /* No REPL on a compiler disk — option 1 is the file selector. */
            cout_str(menu_sel == 0 ? "> " : "  ");
            cout_str("1  File selector\r");
            cout_str(menu_sel == 1 ? "> " : "  ");
            cout_str("2  Debug\r");
            cout_str(menu_sel == 2 ? "> " : "  ");
            cout_str("3  Run tests (data disk)\r");
            cout_str(menu_sel == 3 ? "> " : "  ");
            cout_str("4  About\r");
        } else {
            cout_str(menu_sel == 0 ? "> " : "  ");
            cout_str("1  REPL\r");
            cout_str(menu_sel == 1 ? "> " : "  ");
            cout_str("2  File selector\r");
            cout_str(menu_sel == 2 ? "> " : "  ");
            cout_str("3  Debug\r");
            cout_str(menu_sel == 3 ? "> " : "  ");
            cout_str("4  Run tests (data disk)\r");
            cout_str(menu_sel == 4 ? "> " : "  ");
            cout_str("5  About\r");
        }

#ifdef LITE_IIE
        /* Current text width + the [W] toggle, so the 80-col setting is
         * changeable right from the menu (saved + applied on later boots). [W]
         * is also live in the file browser. //e disk only. */
        a_cout('\r');
        cout_str("Text width - ");
        cout_str(g_width80 ? "80 col  ([W] = 40)\r" : "40 col  ([W] = 80)\r");
#endif

        /* The select prompt (with the input cursor) on the second-last row. */
        a_vtab(22);
        cout_str(g_familyb ? MENU_PROMPT_FB : MENU_PROMPT);

        /* Navigation sub-loop: I/M (or //e Up/Down) move the highlight by
         * repainting only the markers — no full-screen redraw, so it doesn't
         * flicker. Any other key breaks out to the action handler below. */
        for (;;) {
            do { key = a_kbd(); } while (!key);
            key &= 0x7F;
            if (key >= 'a' && key <= 'z') key = (uint8_t)(key - 0x20);
            if (key == 'I' || key == 0x0B) {
                if (menu_sel) { menu_sel--; menu_markers(menu_sel); }
                continue;
            }
            if (key == 'M' || key == 0x0A) {
                if (menu_sel < g_menu_n - 1) { menu_sel++; menu_markers(menu_sel); }
                continue;
            }
            break;
        }

#ifdef LITE_IIE
        if (key == 'W') { width80_toggle(); continue; }   /* toggle text width 40/80 (//e) */
#endif
        /* Resolve the key to an ACTION (0 REPL, 1 File selector, 2 Debug,
         * 3 About). Return or the right arrow ($15/Ctrl-U) activates the
         * highlight (matching the file/volume pickers); D/A are letter aliases;
         * a digit picks by on-screen position. Family B has no REPL, so its
         * positions are shifted up one (1 Files .. 3 About). The Help screen
         * moved to a README.TXT on disk — open it from the File selector. */
        {
            uint8_t pos = 0xFF, act = 0xFF;
            if (key == 0x0D || key == 0x15) pos = menu_sel;
            else if (key >= '1' && key <= '9') pos = (uint8_t)(key - '1');
            if (key == 'D') act = 2;
            else if (key == 'A') act = 4;     /* About is action 4 (after Run tests) */
            else if (pos != 0xFF && pos < g_menu_n)
                act = (uint8_t)(g_familyb ? pos + 1 : pos);
            a_cout('\r');

            if (act == 3) {              /* Run tests — the menu option (no [T] key) */
                chain_testrun();         /* chains TESTRUN.SYSTEM; returns on absence */
                continue;
            }
            if (act == 0) {              /* REPL — this disk's sole interpreter */
                chosen_fname   = sole_fname;
                chosen_display = sole_display;
                break;
            }
            if (act == 1) {              /* File selector */
                /* Opens at the disk picker so the user chooses a volume first
                 * (e.g. drive 2 / the data disk); Esc or Q comes back here.
                 * Picking a disk SET_PREFIXes into it, then the two-pane
                 * browser opens on its root and returns 1 (with the source
                 * staged + the binary to chain) to launch, 0 to come back. */
                if (!volume_picker()) continue;          /* Esc/Q -> menu */
                if (file_browser(&staged_len, 0, sole_fname, sole_display,
                                 &chosen_fname, &chosen_display)) {
                    break;
                }
                continue;
            }
            if (act == 2) {              /* Debug — chains DEBUG.SYSTEM (returns only on open fail) */
                chain_debug();
                continue;                /* DEBUG.SYSTEM missing -> just re-show the menu */
            }
            if (act == 4) {              /* About */
                show_about();
                cout_str("\rPress any key to return > ");
                do { key = a_kbd(); } while (!key);
                continue;
            }
            /* unrecognized key -> fall through and re-show the menu */
        }
        /* anything else: re-show the menu */
    }

    /* Clear to a fresh screen so LOADING (and any chain error) starts on a
     * clean new line, not jammed over the bottom-row menu legend. Name the
     * file being run (still in g_path from the browser's run path, before
     * the prefix-restore below clobbers it) and the binary it chains. */
    a_home();
    cout_str("Loading ");
    if (staged_len) {            /* option 3 run staged a .swift at $0C00 */
        cout_path(g_path);
        cout_str("\rwith ");
    }
    cout_str(chosen_display);
    a_cout('\r');

    /* Restore the boot prefix in case the browser left us in a
     * subdirectory — the interpreter binaries live at the boot volume. */
    if (g_boot_prefix[0]) {
        uint8_t i;
        for (i = 0; i <= g_boot_prefix[0]; i++) g_path[i] = g_boot_prefix[i];
        a_mli_set_prefix();
    }

    *(uint16_t *)STAGED_LEN_ADDR = staged_len;  /* 0 unless option 3 RUN staged */

#ifdef LITE_IIE
    /* The Family B Compiler/Runner are 40-col tools (no 80-col firmware; their
     * char render forks per-machine via WITH_IIE but stays 40-col either way),
     * and they write text straight to main RAM — which an
     * 80-col display renders garbled (and 80STORE would mis-route their MLI
     * reads). Drop to 40-col / main RAM before chaining them, then HOME-clear:
     * switching 80->40 leaves a main-column smear of the 80-col "Loading"
     * line that the tool's own clrscr doesn't wipe in this inherited state.
     * a_home() with g_width80==0 is the monitor's 40-col page clear. The
     * launcher re-enables 80-col from its pref when they chain back. */
    if (g_familyb && g_width80) { width80_off(); a_home(); }
#endif

    g_open_pathname = chosen_fname;
    if (a_mli_open() != 0) {
        /* Single-interpreter REPL model: each REPL image carries ONE interpreter
         * (launcher + lite, OR launcher + this disk's extras binary). The
         * chosen binary isn't on this volume, so chain whichever IS present —
         * the present binary always wins, regardless of which the menu/HW
         * probe picked. The fallback is bidirectional: lite <-> this disk's
         * extras (extras is a lite superset; lite runs a Saturn/aux program's
         * non-extras parts). If neither opens, halt. */
        const uint8_t *alt;
        const char *alt_disp;
        if (chosen_fname == fname_lite) { alt = extras_fname; alt_disp = extras_display; }
        else                            { alt = fname_lite;   alt_disp = LITE_DISPLAY;   }
        cout_str("Not on disk; using ");
        cout_str(alt_disp);
        a_cout('\r');
        chosen_fname = alt;
        g_open_pathname = alt;
        if (a_mli_open() != 0) {
            cout_str("MLI error; halted.\r");
            for (;;) { }
        }
    }
    /* SWIFTSAT + SWIFTAUX use the design-doc-011 packed format (4-byte
     * header + main + extras chunk); their chunked-staging loaders keep
     * MLI's body in built-in LC bank 1 mapped between reads. SWIFTSAT
     * stages the XLC into Saturn bank 1; SWIFTAUX stages the park into
     * aux main RAM via AUXMOVE. Lite uses the original one-READ path.
     * See docs/contributing/design/011-extras-lc-in-saturn-aux.md. */
    if (chosen_fname == fname_swiftsat) {
        a_install_and_chain_swiftsat();  /* never returns */
    } else if (chosen_fname == fname_swiftaux) {
        a_install_and_chain_swiftaux();  /* never returns */
    } else {
        a_install_and_chain();           /* never returns */
    }
    return 0;                            /* unreached */
}
