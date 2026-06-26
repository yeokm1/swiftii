/* tools/apple2/testrun_sys/testrun.c — TESTRUN.SYSTEM, the on-target auto-test
 * harness (design doc 018).
 *
 * A standalone ProDOS SYS tool (load $2000), built like DEBUG.SYSTEM but
 * linking the boot launcher's proven assembly (boot_launcher_asm.s) for the
 * MLI verbs, hardware probes, and chain loaders (a_install_and_chain + the
 * SWIFTSAT/SWIFTAUX chunked-staging variants). It sweeps every self-checking
 * test tier this disk's binary can run — CORE (+ XTESTS on an extras REPL
 * disk), or FBTESTS on a Family B compiler disk — one test per reboot,
 * skipping ERRTESTS (they deliberately fail).
 *
 * The launcher chains here (Run-tests menu, or boot-resume when a TESTRUN note is
 * found). We read the queue cursor from a TESTRUN note on the data disk
 * (parallel to LASTRUN; survives the interpreter's cold-reboot return), stage
 * the next test, advance the note, and chain this disk's interpreter. The
 * test runs; the user reads the `fail 0` line and presses Ctrl-D (one
 * keystroke — the REPL already exits to the launcher on Ctrl-D), which
 * cold-reboots to the launcher, which chains us again. When the queue empties
 * we delete the note, show a summary, and chain the launcher back.
 *
 * Machine-independent: one build serves every disk (like DEBUG.SYSTEM). It
 * detects this disk's sole interpreter by probing file_on_disk.
 */

#include <stdint.h>
#include "../../../src/common/config.h"   /* STAGED_SRC_ADDR / STAGED_LEN_ADDR /
                                        * EDIT_PATH_ADDR / SX_SAT_SLOT_ADDR    */

/* ---- C globals the shared boot_launcher_asm.s imports (exact ABI). ---- */
uint8_t        g_saturn_slot;     /* $FF = none, 0..7 = slot                 */
uint8_t        g_aux_found;       /* 0 = no 64K aux, 1 = real aux            */
uint8_t        g_slot0_readback;  /* probe scratch (unused here, asm needs)  */
uint8_t        g_slot_readback;
const uint8_t *g_open_pathname;   /* MLI OPEN pathname (length-prefixed)     */
uint8_t        g_open_refnum;     /* MLI OPEN refnum out                     */
uint8_t        g_prefix[65];      /* GET_PREFIX out                          */
uint8_t        g_path[65];        /* OPEN / SET_PREFIX target                */
uint8_t        g_path2[65];       /* RENAME new-name (asm ABI; unused here)  */
uint8_t        g_note[96];        /* note serialization buffer               */
uint8_t        g_notepath[80];    /* absolute path of the note file          */
uint16_t       g_note_len;        /* bytes to WRITE from g_note              */
uint8_t        g_online[256];     /* ON_LINE result (16 x 16-byte records)   */
uint8_t        g_width80;         /* 0 = 40-col (always, for this tool)      */
uint8_t        g_rb_unit;         /* READ_BLOCK inputs (asm ABI; unused)     */
uint16_t       g_rb_block;

/* ---- asm helpers (subset of boot_launcher_asm.s). ---- */
extern void     __fastcall__ a_home(void);
extern void     __fastcall__ a_cout(uint8_t c);
extern uint8_t  __fastcall__ a_kbd(void);
extern void     __fastcall__ a_probe_saturn(void);
extern void     __fastcall__ a_probe_aux(void);
extern uint8_t  __fastcall__ a_mli_open(void);
extern uint16_t __fastcall__ a_mli_read_startup(void);
extern void     __fastcall__ a_install_and_chain(void);
extern void     __fastcall__ a_install_and_chain_swiftsat(void);
extern void     __fastcall__ a_install_and_chain_swiftaux(void);
extern uint8_t  __fastcall__ a_mli_get_prefix(void);
extern uint8_t  __fastcall__ a_mli_set_prefix(void);
extern uint16_t __fastcall__ a_mli_read_dirblk(void);
extern uint8_t  __fastcall__ a_mli_close(void);
extern uint8_t  __fastcall__ a_mli_destroy_note(void);
extern uint8_t  __fastcall__ a_mli_create_note(void);
extern void     __fastcall__ a_mli_write_note(void);
extern uint16_t __fastcall__ a_mli_read_note(void);
extern uint8_t  __fastcall__ a_mli_online(void);
extern void     __fastcall__ a_wait(void);   /* ROM WAIT, A=$FF (~0.16 s)      */
extern uint8_t  __fastcall__ a_kbd(void);    /* non-blocking key poll (0=none) */

/* ---- Low-RAM scratch shared with the asm (raw addresses, not segments). -- */
#define DIRBLK    ((volatile uint8_t *)0x0800)  /* one 512 B ProDOS dir block */
#define CUR_TAB   ((uint8_t *)0x1400)           /* parsed entries (32 x 20)   */
#define ROW_BYTES 20
#define MAX_ROWS  32

/* ---- Binary names on this disk (REPL disks carry exactly one interpreter). ---- */
static const uint8_t fname_swiftiip[] = { 15,'S','W','I','F','T','I','I','P','.','S','Y','S','T','E','M' };
static const uint8_t fname_swiftiie[] = { 15,'S','W','I','F','T','I','I','E','.','S','Y','S','T','E','M' };
static const uint8_t fname_swiftsat[] = { 15,'S','W','I','F','T','S','A','T','.','S','Y','S','T','E','M' };
static const uint8_t fname_swiftaux[] = { 15,'S','W','I','F','T','A','U','X','.','S','Y','S','T','E','M' };
static const uint8_t fname_compiler[] = { 15,'C','O','M','P','I','L','E','R','.','S','Y','S','T','E','M' };
static const uint8_t fname_launcher[] = { 14,'S','W','I','F','T','I','I','.','S','Y','S','T','E','M' };

/* ---- Harness state (persisted in the TESTRUN note across reboots). ---- */
static uint8_t  g_familyb;         /* 1 = compiler disk (FBTESTS via Compiler) */
static uint8_t  g_sole_extras;     /* 1 = extras REPL disk (also runs XTESTS) */
static uint8_t  g_boot_prefix[65]; /* the boot volume (interpreters live here)*/
static uint8_t  g_edit_abs[80];    /* Family B: absolute source path for the Compiler */
static uint8_t  g_test_vol[18];    /* "/VOL/" of the disk holding TESTS/      */
static uint8_t  g_test_tier;       /* index into this disk's tier list        */
static uint8_t  g_test_ord;        /* next .swift ordinal within the tier     */
static uint16_t g_test_count;      /* tests launched so far (for the summary) */
static uint8_t  g_selmask;         /* bit i = tier i selected (design doc 018) */
static uint8_t  g_total;           /* total .swift in the selected tiers (M)  */

#define KEY_ESC 0x1B

/* ------------------------------------------------------------------ */
/* Small output + key helpers (ported from boot_launcher.c).          */
/* ------------------------------------------------------------------ */
static void cout_str(const char *s) {
    while (*s) a_cout((uint8_t)*s++);
}

static void cout_u16(uint16_t v) {
    char buf[6];
    uint8_t i = 0;
    if (v == 0) { a_cout('0'); return; }
    while (v) { buf[i++] = (char)('0' + (v % 10)); v /= 10; }
    while (i) a_cout((uint8_t)buf[--i]);
}

static void cout_path(const uint8_t *p) {
    uint8_t i;
    for (i = 1; i <= p[0]; i++) a_cout(p[i]);
}

static uint8_t getkey(void) {
    uint8_t k;
    do { k = a_kbd(); } while (!k);
    return (uint8_t)(k & 0x7F);
}

/* 1 if the row's filename ends in ".SWIFT". */
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

/* ------------------------------------------------------------------ */
/* Directory reading (ported from boot_launcher.c).                   */
/* ------------------------------------------------------------------ */
static void add_entry(uint8_t *table, uint8_t *pcnt, uint8_t storage,
                      const volatile uint8_t *entry) {
    uint8_t namelen = (uint8_t)(entry[0] & 0x0F);
    uint8_t is_dir  = (uint8_t)(storage == 0x0D);
    uint8_t *row;
    uint8_t i;
    if (!namelen) return;
    if (*pcnt >= MAX_ROWS) return;
    row = table + (uint16_t)(*pcnt) * ROW_BYTES;
    row[0] = (uint8_t)(namelen | (is_dir ? 0x80 : 0x00));
    for (i = 0; i < namelen; i++) row[1 + i] = entry[1 + i];
    (*pcnt)++;
}

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
            el  = DIRBLK[0x23];
            epb = DIRBLK[0x24];
            if (el == 0 || epb == 0) { el = 0x27; epb = 0x0D; }
        }
        {
            uint16_t off = 4;
            for (e = 0; e < epb; e++) {
                const volatile uint8_t *entry = DIRBLK + off;
                if (!(first && e == 0)) {
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

/* ------------------------------------------------------------------ */
/* Staging (ported from boot_launcher.c stage_only / fb_abs_from_row).*/
/* ------------------------------------------------------------------ */
static void row_to_path(const uint8_t *row) {
    uint8_t nn = (uint8_t)(row[0] & 0x0F), i;
    g_path[0] = nn;
    for (i = 0; i < nn; i++) g_path[1 + i] = row[1 + i];
}

static void fb_abs_from_row(const uint8_t *row) {
    uint8_t nn = (uint8_t)(row[0] & 0x0F);
    uint8_t pn, k = 0, i;
    a_mli_get_prefix();
    pn = g_prefix[0];
    for (i = 0; i < pn && k < (uint8_t)(sizeof g_edit_abs - 2); i++)
        g_edit_abs[1 + k++] = g_prefix[1 + i];
    for (i = 0; i < nn && k < (uint8_t)(sizeof g_edit_abs - 2); i++)
        g_edit_abs[1 + k++] = row[1 + i];
    g_edit_abs[0] = k;
}

/* Stage a row's program for the chain. Family A reads the source to $0C00;
 * Family B hands the Compiler the source's absolute path. The current prefix
 * must already be the file's directory. Returns the MLI OPEN error (0 = ok). */
static uint8_t stage_only(const uint8_t *row, uint16_t *staged_len_out) {
    uint8_t err;
    row_to_path(row);
    if (g_familyb) {
        volatile uint8_t *dst = (volatile uint8_t *)EDIT_PATH_ADDR;
        uint8_t i;
        fb_abs_from_row(row);
        for (i = 0; i <= g_edit_abs[0]; i++) dst[i] = g_edit_abs[i];
        *staged_len_out = 0;
        return 0;
    }
    g_open_pathname = g_path;
    err = a_mli_open();
    if (err) return err;
    *staged_len_out = a_mli_read_startup();
    return 0;
}

/* ------------------------------------------------------------------ */
/* TESTRUN note (parallel to the launcher's LASTRUN).                 */
/* Body: [tier][ord][countLo][countHi] at /VOL/TESTRUN.              */
/* ------------------------------------------------------------------ */
static void note_build_path(const uint8_t *prefix, const char *tag) {
    uint8_t i, k = 0, slashes = 0;
    for (i = 1; i <= prefix[0]; i++) {
        g_notepath[1 + k++] = prefix[i];
        if (prefix[i] == '/' && ++slashes == 2) break;   /* through "/VOL/" */
    }
    for (i = 0; i < 7; i++) g_notepath[1 + k++] = (uint8_t)tag[i];
    g_notepath[0] = k;
}

static void test_note_save(void) {
    g_note[0] = g_selmask;
    g_note[1] = g_test_tier;
    g_note[2] = g_test_ord;
    g_note[3] = (uint8_t)(g_test_count & 0xFF);
    g_note[4] = (uint8_t)(g_test_count >> 8);
    g_note[5] = g_total;
    g_note_len = 6;
    note_build_path(g_test_vol, "TESTRUN");
    g_open_pathname = g_notepath;
    a_mli_destroy_note();
    if (a_mli_create_note() != 0) return;
    if (a_mli_open() != 0) return;
    a_mli_write_note();
    a_mli_close();
}

static void test_note_destroy(void) {
    note_build_path(g_test_vol, "TESTRUN");
    g_open_pathname = g_notepath;
    a_mli_destroy_note();
}

/* Find + parse an in-progress TESTRUN note on any online volume into the
 * g_test_* globals (+ g_test_vol). Returns 1 if a run is in progress. */
static uint8_t test_note_consume(void) {
    uint8_t v, j, k;
    uint16_t got;
    for (v = 0; v < 16; v++) g_online[(uint16_t)v * 16] = 0;
    a_mli_online();
    for (v = 0; v < 16; v++) {
        const uint8_t *rec = g_online + (uint16_t)v * 16;
        uint8_t nlen = (uint8_t)(rec[0] & 0x0F);
        if (nlen < 1 || nlen > 15) continue;
        k = 0;
        g_test_vol[1 + k++] = '/';
        for (j = 0; j < nlen; j++) g_test_vol[1 + k++] = rec[1 + j];
        g_test_vol[1 + k++] = '/';
        g_test_vol[0] = k;
        note_build_path(g_test_vol, "TESTRUN");
        g_open_pathname = g_notepath;
        if (a_mli_open() != 0) continue;
        got = a_mli_read_note();
        a_mli_close();
        if (got < 6) continue;
        g_selmask    = g_note[0];
        g_test_tier  = g_note[1];
        g_test_ord   = g_note[2];
        g_test_count = (uint16_t)(g_note[3] | (g_note[4] << 8));
        g_total      = g_note[5];
        return 1;
    }
    return 0;
}

/* Scan online volumes for one with a TESTS/ directory; record "/VOL/" in
 * g_test_vol. Returns 1 if found. */
static uint8_t find_test_vol(void) {
    static const char tag[] = "TESTS";
    uint8_t v, j, k;
    for (v = 0; v < 16; v++) g_online[(uint16_t)v * 16] = 0;
    a_mli_online();
    for (v = 0; v < 16; v++) {
        const uint8_t *rec = g_online + (uint16_t)v * 16;
        uint8_t nlen = (uint8_t)(rec[0] & 0x0F);
        if (nlen < 1 || nlen > 15) continue;
        k = 0;
        g_path[1 + k++] = '/';
        for (j = 0; j < nlen; j++) g_path[1 + k++] = rec[1 + j];
        g_path[1 + k++] = '/';
        for (j = 0; j < 5; j++)    g_path[1 + k++] = (uint8_t)tag[j];
        g_path[0] = k;                              /* "/VOL/TESTS" */
        g_open_pathname = g_path;
        if (a_mli_open() != 0) continue;
        a_mli_close();
        k = 0;
        g_test_vol[1 + k++] = '/';
        for (j = 0; j < nlen; j++) g_test_vol[1 + k++] = rec[1 + j];
        g_test_vol[1 + k++] = '/';
        g_test_vol[0] = k;
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Tier resolution + the run loop.                                    */
/* ------------------------------------------------------------------ */
static const char *tier_name(uint8_t idx) {
    if (g_familyb) {
        /* The compiler-runner is a superset of the REPL: it runs the general
         * CORE tests and the extras XTESTS too (the fbtests README confirms
         * XCONV/XARRAY run on it), plus its own Family-B FBTESTS. */
        if (idx == 0) return "CORE";
        if (idx == 1) return "XTESTS";
        if (idx == 2) return "FBTESTS";
        return (const char *)0;
    }
    if (idx == 0) return "CORE";
    if (idx == 1 && g_sole_extras) return "XTESTS";   /* extras REPL only */
    return (const char *)0;
}

/* g_path = "/VOL/TESTS/<tier>" (slash=0, for read_dir) or ".../<tier>/"
 * (slash=1, for SET_PREFIX). */
static void build_tier_path(const char *tier, uint8_t slash) {
    static const char mid[] = "TESTS/";
    uint8_t k = 0, i;
    for (i = 1; i <= g_test_vol[0]; i++) g_path[1 + k++] = g_test_vol[i];
    for (i = 0; i < 6; i++)              g_path[1 + k++] = (uint8_t)mid[i];
    for (i = 0; tier[i]; i++)            g_path[1 + k++] = (uint8_t)tier[i];
    if (slash)                          g_path[1 + k++] = '/';
    g_path[0] = k;
}

static uint8_t find_nth_swift(uint8_t n, uint8_t count) {
    uint8_t i, m = 0;
    for (i = 0; i < count; i++) {
        const uint8_t *row = CUR_TAB + (uint16_t)i * ROW_BYTES;
        if (row[0] & 0x80) continue;          /* subdirectory */
        if (!row_is_swift(row)) continue;     /* not a .swift  */
        if (m == n) return i;
        m++;
    }
    return 0xFF;
}

/* Restore the boot prefix and chain a SYS binary (the interpreter or the
 * launcher). Never returns on success. */
static void chain_bin(const uint8_t *name, uint16_t staged_len) {
    uint8_t i;
    if (g_boot_prefix[0]) {
        for (i = 0; i <= g_boot_prefix[0]; i++) g_path[i] = g_boot_prefix[i];
        a_mli_set_prefix();
    }
    *(volatile uint16_t *)STAGED_LEN_ADDR = staged_len;
    *(volatile uint8_t  *)SX_SAT_SLOT_ADDR = g_saturn_slot;
    g_open_pathname = name;
    if (a_mli_open() != 0) {
        a_home();
        cout_str("CANNOT OPEN ");
        cout_path(name);
        cout_str("\rHALTED.\r");
        for (;;) { }
    }
    if (name == fname_swiftsat)      a_install_and_chain_swiftsat();
    else if (name == fname_swiftaux) a_install_and_chain_swiftaux();
    else                             a_install_and_chain();
    for (;;) { }                                 /* unreached */
}

/* Number of test tiers this disk can run (CORE[+XTESTS], or FBTESTS). */
static uint8_t tier_count(void) {
    uint8_t n = 0;
    while (tier_name(n) != (const char *)0) n++;
    return n;
}

/* Total .swift across the SELECTED tiers (the countdown's "of M"). */
static uint8_t count_selected(void) {
    uint8_t t, cnt, i, sum = 0;
    for (t = 0; tier_name(t) != (const char *)0; t++) {
        if (!((g_selmask >> t) & 1)) continue;
        build_tier_path(tier_name(t), 0);
        cnt = 0;
        if (read_dir(g_path, CUR_TAB, &cnt) != 0) cnt = 0;
        for (i = 0; i < cnt; i++) {
            const uint8_t *row = CUR_TAB + (uint16_t)i * ROW_BYTES;
            if (!(row[0] & 0x80) && row_is_swift(row)) sum++;
        }
    }
    return sum;
}

/* Wait ~1 s, polling for a key. Returns the key (0x7F-masked), or 0 on
 * timeout. a_wait is ~0.16 s, so 6 of them ~= 1 s. */
static uint8_t wait_about_1s(void) {
    uint8_t n, k;
    for (n = 0; n < 6; n++) {
        k = a_kbd();
        if (k) return (uint8_t)(k & 0x7F);
        a_wait();
    }
    return 0;
}

/* Per-test countdown. Returns 1 to run the test, 0 if Esc was pressed
 * (terminate the sweep -> results). Any other key skips the remaining wait. */
static uint8_t countdown(uint16_t n, uint8_t total, const uint8_t *name) {
    uint8_t s, k;
    a_home();
    cout_str("TEST ");
    cout_u16(n);
    cout_str(" OF ");
    cout_u16(total);
    cout_str(":\r");
    cout_path(name);
    cout_str("\r\r");
    /* Family B: a running tally of how many test FILES have passed/failed so
     * far (from the Runner's TESTLOG) — so the per-test "fail N" check counts
     * a test prints aren't mistaken for failed files. */
    if (g_familyb) {
        uint16_t got = 0;
        uint8_t  i, np = 0, nf = 0;
        note_build_path(g_test_vol, "TESTLOG");
        g_open_pathname = g_notepath;
        if (a_mli_open() == 0) { got = a_mli_read_note(); a_mli_close(); }
        for (i = 0; i < got; i++) { if (g_note[i] == 'F') nf++; else np++; }
        if (got) {
            cout_str("FILES SO FAR: ");
            cout_u16(np); cout_str(" OK, ");
            cout_u16(nf); cout_str(" FAIL\r\r");
        }
    }
    cout_str("ESC = STOP AND SEE RESULTS.\r\r");
    cout_str("STARTING IN ");
    for (s = 3; s >= 1; s--) {
        cout_u16(s);
        a_cout(' ');
        k = wait_about_1s();
        if (k == KEY_ESC) return 0;
        if (k) break;                            /* any other key skips */
    }
    return 1;
}

/* End-of-sweep (or Esc-terminate) results page. Esc -> main menu. Never
 * returns. REPL tiers show only the count (no verdict channel); Family B's
 * per-test PASS/FAIL from the TESTLOG is filled in by a later slice. */
static void results_page(void) {
    test_note_destroy();
    /* The Compiler writes each test's .swb next to its source on the data disk;
     * the Runner deletes its own .swb after loading it (runner_main.c, TESTRUN
     * mode), so the sweep leaves no .swb behind — nothing to clean up here. */
    /* Family B: read the per-test verdicts the Runner logged to TESTLOG
     * (P/F per test, in run order) so we can list the failures by name. */
    {
        uint16_t got = 0;
        uint8_t  i, nfail = 0;
        if (g_familyb) {
            note_build_path(g_test_vol, "TESTLOG");
            g_open_pathname = g_notepath;
            if (a_mli_open() == 0) { got = a_mli_read_note(); a_mli_close(); }
            for (i = 0; i < got; i++) if (g_note[i] == 'F') nfail++;
        }
        a_home();
        cout_str("=== TEST RUN RESULTS ===\r\r");
        cout_str("RAN ");
        cout_u16(g_test_count);
        cout_str(" OF ");
        cout_u16(g_total);
        cout_str(" TEST");
        if (g_total != 1) a_cout('S');
        cout_str(".\r\r");
        if (!g_familyb) {
            cout_str("YOU READ EACH RESULT ABOVE\r(\"FAIL 0\" = PASSED).\r\r");
        } else {
            cout_str("PASSED ");
            cout_u16((uint16_t)(got - nfail));
            cout_str(",  FAILED ");
            cout_u16(nfail);
            cout_str(".\r\r");
            if (nfail) {
                uint16_t g = 0;
                uint8_t t, cnt;
                cout_str("FAILED:\r");
                for (t = 0; tier_name(t) != (const char *)0; t++) {
                    if (!((g_selmask >> t) & 1)) continue;
                    build_tier_path(tier_name(t), 0);
                    cnt = 0;
                    if (read_dir(g_path, CUR_TAB, &cnt) != 0) cnt = 0;
                    for (i = 0; i < cnt; i++) {
                        const uint8_t *row = CUR_TAB + (uint16_t)i * ROW_BYTES;
                        if ((row[0] & 0x80) || !row_is_swift(row)) continue;
                        if (g < got && g_note[g] == 'F') {
                            uint8_t nn = (uint8_t)(row[0] & 0x0F), j;
                            cout_str("  ");
                            for (j = 0; j < nn; j++) a_cout(row[1 + j]);
                            a_cout('\r');
                        }
                        g++;
                    }
                }
                a_cout('\r');
            }
        }
        cout_str("PRESS ESC TO RETURN TO MENU.\r");
    }
    while (getkey() != KEY_ESC) { /* wait for Esc */ }
    chain_bin(fname_launcher, 0);                /* never returns */
}

/* Stage + chain the next SELECTED-tier test, advancing the TESTRUN cursor,
 * after a countdown (Esc there -> results). Never returns: it chains the
 * interpreter, or (queue empty / Esc) goes to the results page. */
static void run_next(const uint8_t *sole) {
    uint16_t staged_len = 0;
    for (;;) {
        const char *tier = tier_name(g_test_tier);
        uint8_t cnt = 0, idx;
        if (tier == (const char *)0) results_page();        /* never returns */
        if (!((g_selmask >> g_test_tier) & 1)) {            /* tier not chosen */
            g_test_tier++; g_test_ord = 0; continue;
        }
        build_tier_path(tier, 0);                  /* /VOL/TESTS/<TIER> */
        if (read_dir(g_path, CUR_TAB, &cnt) != 0) cnt = 0;
        idx = find_nth_swift(g_test_ord, cnt);
        if (idx == 0xFF) { g_test_tier++; g_test_ord = 0; continue; }
        build_tier_path(tier, 1);                  /* prefix = the tier dir */
        a_mli_set_prefix();
        if (stage_only(CUR_TAB + (uint16_t)idx * ROW_BYTES, &staged_len) != 0) {
            g_test_ord++;                          /* unreadable -> skip it */
            continue;
        }
        g_test_ord++;
        g_test_count++;
        test_note_save();                          /* persist the advanced cursor */
        /* g_path holds the test's filename (row_to_path); show the countdown
         * before chaining. Esc terminates the sweep. */
        if (!countdown(g_test_count, g_total, g_path)) results_page();  /* never returns */
        if (g_familyb) {
            /* Family B advance is the Runner's own end-of-run pause, NOT
             * Ctrl-D — set the marker so it auto-advances (it rides the $1B00
             * page through the COMPILER->RUNNER chain, like the Saturn slot). */
            *(volatile uint8_t *)TESTRUN_MODE_ADDR       = TESTRUN_MODE_MAGIC0;
            *(volatile uint8_t *)(TESTRUN_MODE_ADDR + 1) = TESTRUN_MODE_MAGIC1;
        }
        chain_bin(sole, staged_len);               /* never returns */
    }
}

/* Intro screen (fresh start). Any key begins; Esc -> main menu. */
static uint8_t intro_screen(void) {
    a_home();
    cout_str("=== SWIFTII TEST RUNNER ===\r\r");
    cout_str("RUNS THE ON-DISK SELF-CHECKING\r");
    cout_str("TESTS ONE AFTER ANOTHER. EACH\r");
    cout_str("PRINTS ITS RESULT; A LAST LINE\r");
    cout_str("\"FAIL 0\" MEANS IT PASSED.\r\r");
    if (g_familyb)
        cout_str("TESTS AUTO-ADVANCE AFTER A PAUSE.\r");
    else
        cout_str("PRESS CTRL-D AFTER EACH TO GO ON.\r");
    cout_str("A COUNTDOWN PLAYS BETWEEN TESTS;\r");
    cout_str("ESC THERE STOPS + SHOWS RESULTS.\r\r");
    cout_str("ANY KEY = BEGIN\r\r");
    cout_str("ESC     = BACK TO MENU\r");
    return (uint8_t)(getkey() != KEY_ESC);
}

/* Tier-selection checklist (only when the disk has >=2 tiers). All selected
 * by default. 1/2 toggle, Ret or right-arrow starts, Esc -> menu. Returns 1 to
 * start. Right-arrow ($15/Ctrl-U) mirrors the launcher menu/pickers. */
static uint8_t selection_screen(void) {
    uint8_t k, i, n = tier_count();
    for (;;) {
        a_home();
        cout_str("SELECT TESTS TO RUN:\r\r");
        for (i = 0; i < n; i++) {
            a_cout((uint8_t)('1' + i));
            cout_str(((g_selmask >> i) & 1) ? "  [X] " : "  [ ] ");
            cout_str(tier_name(i));
            a_cout('\r');
        }
        a_cout('\r');
        cout_str("NUMBER   = TOGGLE A TIER\r\r");
        cout_str("RET/->   = RUN SELECTED\r\r");
        cout_str("ESC      = BACK TO MENU\r");
        k = getkey();
        if (k == KEY_ESC) return 0;
        if (k == 0x0D || k == 0x15) { if (g_selmask) return 1; continue; }
        if (k >= '1' && k < (uint8_t)('1' + n))
            g_selmask ^= (uint8_t)(1u << (k - '1'));
    }
}

static uint8_t file_on_disk(const uint8_t *name) {
    g_open_pathname = name;
    if (a_mli_open() != 0) return 0;
    a_mli_close();
    return 1;
}

int main(void) {
    const uint8_t *sole;

    __asm__("bit $C082");                          /* ROM-read for ROM calls */

    a_home();
    cout_str("SWIFTII TEST RUNNER\r\r");

    a_probe_saturn();                              /* sets g_saturn_slot */
    a_probe_aux();

    if (a_mli_get_prefix() == 0) {
        uint8_t i;
        for (i = 0; i <= g_prefix[0]; i++) g_boot_prefix[i] = g_prefix[i];
    } else {
        g_boot_prefix[0] = 0;
    }

    /* This disk's sole interpreter, if this is a REPL disk. */
    if (file_on_disk(fname_swiftsat)) {
        sole = fname_swiftsat; g_sole_extras = 1;
    } else if (file_on_disk(fname_swiftaux)) {
        sole = fname_swiftaux; g_sole_extras = 1;
    } else if (file_on_disk(fname_compiler)) {
        sole = fname_compiler; g_familyb = 1;
    } else if (file_on_disk(fname_swiftiie)) {
        sole = fname_swiftiie;
    } else {
        sole = fname_swiftiip;
    }
    *(volatile uint8_t *)EDIT_PATH_ADDR = 0;       /* clear any stale path */

    /* Resume an in-progress sweep (note holds selmask/cursor/total), or run
     * the fresh-start screens: intro -> tier selection -> countdown sweep. */
    if (!test_note_consume()) {
        if (!find_test_vol()) {
            a_home();
            cout_str("NO TESTS/ FOLDER FOUND ON A\rMOUNTED DISK.\r\r");
            cout_str("PUT THE DATA DISK IN DRIVE 2,\rTHEN RUN THE TESTS AGAIN.\r\r");
            cout_str("PRESS A KEY TO RETURN TO MENU.\r");
            (void)getkey();
            chain_bin(fname_launcher, 0);          /* never returns */
        }
        if (!intro_screen()) chain_bin(fname_launcher, 0);     /* Esc -> menu */
        g_selmask = (uint8_t)((1u << tier_count()) - 1);       /* all by default */
        if (tier_count() >= 2 && !selection_screen())
            chain_bin(fname_launcher, 0);                      /* Esc -> menu */
        g_total = count_selected();
        if (g_total == 0) {
            a_home();
            cout_str("NO TESTS IN THE SELECTED TIERS.\r\r");
            cout_str("PRESS A KEY TO RETURN TO MENU.\r");
            (void)getkey();
            chain_bin(fname_launcher, 0);          /* never returns */
        }
        g_test_tier = 0;
        g_test_ord = 0;
        g_test_count = 0;
        /* Family B: start the TESTLOG fresh — the Runner appends one P/F per
         * test, the results page reads it back to list failures (doc 018). */
        if (g_familyb) {
            note_build_path(g_test_vol, "TESTLOG");
            g_open_pathname = g_notepath;
            a_mli_destroy_note();                  /* clear any prior log */
        }
        test_note_save();                          /* persist selmask + total */
    }

    run_next(sole);                                /* never returns */
    return 0;                                       /* unreached */
}
