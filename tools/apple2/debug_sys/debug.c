/* DEBUG.SYSTEM — standalone hardware / diagnostics screen.
 *
 * The boot launcher used to draw a small detection screen in-process (menu
 * option Debug), but its code sat in the launcher's near-zero BSS headroom.
 * Moving it into its own ProDOS SYS binary freed that room to fund the II+
 * editor's typing fast path. The launcher chains here from the Debug menu
 * (MLI OPEN/READ to $2000); this draws three paged diagnostic screens, waits
 * for a key between them, and chains back to SWIFTII.SYSTEM.
 *
 * This is also where the volume-picker DISK-SPACE readout lives for the II+:
 * the II+ launcher's BSS is pinned at its ceiling by the editor fast paths, so
 * it can't carry the per-volume free/total readout (the //e launcher still does,
 * in its file selector). DEBUG.SYSTEM loads at $2000 and is address-space-rich,
 * so it can show the readout — plus a broader hardware survey — on every machine.
 *
 * The two values the boot probe DERIVED that page 1 reports — the slot-0 and
 * slot-N Saturn $D000 readbacks — are handed over by the launcher in low RAM at
 * $1B80 (below the $2000 load region, so they survive the chain READ).
 * Everything else this program reads itself: FBB3 (ROM, after banking ROM in),
 * the ProDOS global page ($BFxx), slot ROM signatures ($Cn0x), and a set of
 * read-only MLI verbs (ON_LINE / READ_BLOCK / GET_PREFIX) wrapped in
 * debug_asm.s.
 *
 * Built as a small cc65 apple2 SYS binary (crt0_ibasic.s + swiftii-system.cfg,
 * load $2000) plus debug_asm.s. It does NOT link the launcher's object. */
#include <stdint.h>

extern void    __fastcall__ a_home(void);
extern void    __fastcall__ a_cout(uint8_t c);
extern void    __fastcall__ a_vtab(uint8_t row);
extern void    __fastcall__ a_print_hex(uint8_t b);
extern uint8_t __fastcall__ a_kbd_wait(void);
extern void    __fastcall__ a_chain_launcher(void);  /* chains; returns only on open fail */
extern uint8_t __fastcall__ a_cpu_is_65c02(void);
extern uint8_t __fastcall__ a_aux_text_present(void);   /* //e only — see asm */
extern uint8_t __fastcall__ a_mli_online(void);
extern uint8_t __fastcall__ a_mli_read_block(void);
extern uint8_t __fastcall__ a_mli_get_prefix(void);

/* Buffers shared with debug_asm.s's MLI param blocks. */
uint8_t  g_online[256];   /* ON_LINE records (16 B each)                       */
uint8_t  g_rb_unit;       /* READ_BLOCK unit_num (DSSS0000)                     */
uint16_t g_rb_block;      /* READ_BLOCK logical block                           */
uint8_t  g_prefix[65];    /* GET_PREFIX result (len-prefixed)                   */

#define DIRBLK     ((volatile uint8_t *)0x0800u)  /* READ_BLOCK target (512 B) */
#define DEBUG_PARK ((const volatile uint8_t *)0x1B80u) /* launcher Saturn bytes */

/* Hardware/ROM probe bytes captured up front in main(): FBB3 must be read with
 * motherboard ROM banked in and BEFORE any MLI call (MLI can leave the language
 * card in RAM-read mode, which would make a later FBB3 read return garbage). */
static uint8_t pb_fbb3, pb_machid, pb_d0s0, pb_d0sn, pb_vdx5, pb_vdx7;
/* ProDOS DEVNUM ($BF30), the last-accessed device, captured at main() entry
 * before our own volume scan re-reads disks (each MLI block read overwrites it).
 * At entry it still reflects the drive DEBUG.SYSTEM was loaded from = boot drive.
 * Format: D SSS dddd (bit 7 = drive, bits 6-4 = slot). */
static uint8_t pb_devnum;
/* Definitive Saturn verdict the launcher parked at $1B82: 0..7 = slot, $FF = none
 * (a_probe_saturn's multi-bank check, not the raw $D000 readback heuristic). */
static uint8_t pb_sat_slot;

static void cout_str(const char *s) {
    while (*s) a_cout((uint8_t)*s++);
}

/* Print an unsigned 16-bit value in decimal. */
static void cout_u16(uint16_t v) {
    uint8_t buf[5], i = 0;
    if (!v) { a_cout('0'); return; }
    while (v) { buf[i++] = (uint8_t)('0' + (v % 10)); v /= 10; }
    while (i) a_cout(buf[--i]);
}

/* Decimal digit count of `v` (>=1), for right-justified padding. */
static uint8_t u16_digits(uint16_t v) {
    uint8_t d = 1;
    while (v >= 10) { v /= 10; d++; }
    return d;
}

/* Print `v` right-justified to `w` columns (leading spaces) so the volume
 * picker's free/total fields line up regardless of digit count. */
static void cout_u16_rj(uint16_t v, uint8_t w) {
    uint8_t d = u16_digits(v);
    while (d++ < w) a_cout(' ');
    cout_u16(v);
}

static void cout_byte(const char *label, uint8_t v, const char *tail) {
    cout_str(label);
    a_print_hex(v);
    cout_str(tail);
}

/* Clear screen and print a page title; ~20 rows remain below for content.
 * All on-screen text is UPPERCASE ASCII — the II+ has no lowercase character
 * generator, so lowercase via ROM COUT displays as garbage. Uppercase is the
 * lowest common denominator that renders correctly on II+ and //e alike. */
static void page_header(const char *title) {
    a_home();
    cout_str("SWIFTII DEBUG   PAGE ");
    cout_str(title);
    cout_str("\r\r");
}

/* Pin the navigation legend to the bottom row (left/right arrows page, ESC
 * quits). Row 22 leaves row 23 clear so the line never scrolls the screen. */
static void page_legend(void) {
    a_vtab(22);
    cout_str("<- ->  PAGE      ESC  EXIT");
}

/* ---- Page 2: DETECTION ---------------------------------------------------- */

static void cout_machine(void) {
    cout_str("MACHINE ");
    /* Apple Tech Note Misc #7: $EA = II+, $38 = original II, $06 = //e/c/gs. */
    cout_str(pb_fbb3 == 0xEA ? "II+" :
             pb_fbb3 == 0x06 ? "//E" :
             pb_fbb3 == 0x38 ? "II"  : "??");
    cout_str((pb_machid & 0x10) ? " 128K" : " 64K");
    if (pb_sat_slot != 0xFF) cout_str(" SATURN");  /* definitive probe verdict  */
    if (pb_vdx5 == 0x38 && pb_vdx7 == 0x18) cout_str(" VIDEX"); /* slot-3 80col */
    a_cout('\r');
}

static void page_detection(void) {
    uint8_t devnum;
    page_header("2/3  DETECTION");
    cout_byte("FBB3    $", pb_fbb3,   "  $EA=II+ $38=II $06=//E\r");
    cout_byte("MACHID  $", pb_machid, "  $10=AUX\r");
    cout_byte("D000-S0 $", pb_d0s0,   "  $A5=SAT S0\r");
    cout_byte("D000-SN $", pb_d0sn,   "  EOR=SAT\r");
    cout_byte("VDXC305 $", pb_vdx5,   "  $38=VDX\r");
    cout_byte("VDXC307 $", pb_vdx7,   "  $18=VDX\r");
    a_cout('\r');
    cout_machine();
    cout_str("CPU     ");
    cout_str(a_cpu_is_65c02() ? "65C02\r" : "6502\r");
    cout_str("SATURN  ");                           /* definitive probe verdict */
    if (pb_sat_slot != 0xFF) {
        cout_str("SLOT ");
        a_cout((uint8_t)('0' + (pb_sat_slot & 7)));
        cout_str(" (128K)\r");
    } else {
        cout_str("NONE\r");
    }
    /* //e aux configuration. MACHID bit 4 is ProDOS's own 64K-aux verdict (an
     * Extended 80-Column Card / 128K). If that's clear, distinguish a plain
     * 80-Column Card (1K aux, text page only) from no aux at all by probing the
     * aux text-page RAM directly. Only //e-class machines ($06) have aux RAM;
     * II ($38) and II+ ($EA) never do. */
    if (pb_fbb3 != 0x06) {
        cout_str("AUX RAM N/A (NOT //E)\r");
    } else if (pb_machid & 0x10) {
        cout_str("AUX RAM EXTENDED 64K (128K)\r");
    } else if (a_aux_text_present()) {
        cout_str("AUX RAM 80-COL CARD (1K)\r");
    } else {
        cout_str("AUX RAM NONE (64K, 40-COL)\r");
    }
    cout_byte("PRODOS  $BFFF=$", *(volatile uint8_t *)0xBFFFu, "\r");
    devnum = pb_devnum;                             /* DEVNUM: D SSS dddd */
    cout_str("BOOT    SLOT ");
    a_cout((uint8_t)('0' + ((devnum >> 4) & 7)));
    cout_str(" DRIVE ");
    a_cout((devnum & 0x80) ? '2' : '1');
    a_cout('\r');
    a_mli_get_prefix();
    cout_str("PREFIX  ");
    {
        uint8_t n = g_prefix[0], i;
        for (i = 1; i <= n; i++) a_cout(g_prefix[i]);
    }
    a_cout('\r');
}

/* ---- Page 3: SLOTS -------------------------------------------------------- */

static void page_slots(void) {
    uint8_t n;
    page_header("3/3  SLOTS");

    /* Select peripheral-card ROM so $Cn0x reads the card signatures (a //e may
     * have its internal $C3 80-col ROM latched in). Harmless on a II+. We leave
     * it in peripheral mode (ProDOS's normal state) afterward. */
    __asm__("sta $C006");

    for (n = 1; n <= 7; n++) {
        uint16_t base = (uint16_t)(0xC000u + ((uint16_t)n << 8));
        uint8_t b1 = *(volatile uint8_t *)(base + 1);
        uint8_t b5 = *(volatile uint8_t *)(base + 5);
        uint8_t b7 = *(volatile uint8_t *)(base + 7);
        uint8_t bc = *(volatile uint8_t *)(base + 0x0C);
        a_cout('S'); a_cout((uint8_t)('0' + n)); cout_str("  ");
        if (b1 == 0x20 && b5 == 0x03) {
            cout_str("DISK II\r");
        } else if (b5 == 0x38 && b7 == 0x18) {     /* Pascal 1.1 firmware ID    */
            uint8_t hn = (uint8_t)(bc & 0xF0);
            if      (hn == 0x80) cout_str("80-COL\r");
            else if (hn == 0x30) cout_str("SERIAL\r");
            else if (hn == 0x20) cout_str("MOUSE\r");
            else                 cout_str("PASCAL DEV\r");
        } else {
            cout_str("ID $"); a_print_hex(b5);
            cout_str(" $");   a_print_hex(b7); a_cout('\r');
        }
    }
}

/* ---- Page 1: VOLUMES (disk space) ---------------------------------------- */

/* Read a volume's free + total block counts from device `unit` (a ProDOS
 * unit_num, DSSS0000). Reads the volume-directory key block (block 2) for
 * total_blocks and the bitmap pointer, then sums the free bits across the
 * allocation bitmap block(s) — a set bit means the block is free. ProDOS has
 * no "blocks free" MLI call, so this mirrors what BASIC.SYSTEM's CATALOG does.
 * Returns 1 with the out-params filled, or 0 if a device read failed.
 * (Ported from the //e launcher's volume picker.) */
static uint8_t vol_blocks(uint8_t unit, uint16_t *out_free, uint16_t *out_total) {
    uint16_t total, bmptr, remaining, blk, freeb = 0;

    g_rb_unit  = unit;
    g_rb_block = 2;                 /* volume directory key block */
    if (a_mli_read_block()) return 0;
    total = (uint16_t)(DIRBLK[0x29] | (DIRBLK[0x2A] << 8));
    bmptr = (uint16_t)(DIRBLK[0x27] | (DIRBLK[0x28] << 8));
    if (total == 0) return 0;

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

static void page_volumes(void) {
    static uint8_t idx[14];   /* g_online record numbers that hold a volume */
    uint8_t count = 0, i, j;

    page_header("1/3  VOLUMES");

    for (i = 0; i < 16; i++) g_online[(uint16_t)i * 16] = 0;
    a_mli_online();
    for (i = 0; i < 16 && count < 14; i++) {
        uint8_t nlen = (uint8_t)(g_online[(uint16_t)i * 16] & 0x0F);
        if (nlen >= 1 && nlen <= 15) idx[count++] = i;
    }

    cout_str("VOLUMES ONLINE: ");
    cout_u16(count);
    a_cout('\r');
    /* Two-column table: KB and blocks, each as free/total right-justified to 5
     * columns so the digits and the '/' separators line up under FREE/TOTAL on
     * every row. The name field is a fixed 12 (the longest volume name we ship,
     * PRODOS.2.4.3); FREE right-aligns into its 5-wide field, TOTAL fills it.
     * Header row 1 = the unit labels; row 2 = FREE/TOTAL under each group. */
    cout_str("                   KB         BLOCKS\r");
    cout_str("               FREE/TOTAL   FREE/TOTAL\r");

    for (i = 0; i < count; i++) {
        const uint8_t *rec = g_online + (uint16_t)idx[i] * 16;
        uint8_t nn = (uint8_t)(rec[0] & 0x0F);
        uint8_t unit = (uint8_t)(rec[0] & 0xF0);
        uint16_t vfree, vtot;
        a_cout('/');
        for (j = 0; j < nn; j++) a_cout(rec[1 + j]);
        for (j = nn; j < 12; j++) a_cout(' ');    /* pad name to 12 cols */
        a_cout(' ');
        if (vol_blocks(unit, &vfree, &vtot)) {
            /* vol_blocks returns BLOCKS; 1 block = 0.5 KB, so KB = blocks >> 1. */
            cout_u16_rj((uint16_t)(vfree >> 1), 5); a_cout('/');
            cout_u16_rj((uint16_t)(vtot  >> 1), 5); a_cout('K');
            a_cout(' ');
            cout_u16_rj(vfree, 5); a_cout('/');
            cout_u16_rj(vtot,  5); a_cout('B');
        } else {
            cout_str("(NO INFO)");
        }
        a_cout('\r');
    }
}

int main(void) {
    /* Bank motherboard ROM in: crt0 leaves the language card in RAM-read mode,
     * but COUT ($FDED) and the $FBB3 machine-ID byte live in ROM. Capture the
     * ROM/hardware probe bytes here, before any MLI call can re-bank the LC. */
    __asm__("bit $C082");
    pb_fbb3   = *(volatile uint8_t *)0xFBB3u;
    pb_machid = *(volatile uint8_t *)0xBF98u;
    pb_d0s0   = DEBUG_PARK[0];
    pb_d0sn   = DEBUG_PARK[1];
    pb_sat_slot = DEBUG_PARK[2];
    pb_vdx5   = *(volatile uint8_t *)0xC305u;
    pb_vdx7   = *(volatile uint8_t *)0xC307u;
    pb_devnum = *(volatile uint8_t *)0xBF30u;  /* boot drive, before we re-read disk */

    /* Page through the diagnostic with the left/right arrows (the II+ has no
     * up/down keys); ESC exits and chains back to SWIFTII.SYSTEM. Redraw only on
     * a page change so a stray key doesn't re-read disk (the volumes page
     * re-reads disk via ON_LINE / READ_BLOCK). */
    {
        uint8_t page = 0, redraw = 1, key;
        for (;;) {
            if (redraw) {
                switch (page) {
                    case 0: page_volumes();   break;
                    case 1: page_detection(); break;
                    default: page_slots();    break;
                }
                page_legend();
                redraw = 0;
            }
            key = a_kbd_wait();
            if (key == 0x1B) break;                       /* ESC -> exit        */
            if (key == 0x15 && page < 2) { page++; redraw = 1; }  /* right arrow */
            if (key == 0x08 && page > 0) { page--; redraw = 1; }  /* left arrow  */
        }
    }

    a_chain_launcher();              /* chains SWIFTII.SYSTEM; never returns on success */
    cout_str("\rNO SWIFTII.SYSTEM\r");
    for (;;) { }
    return 0;
}
