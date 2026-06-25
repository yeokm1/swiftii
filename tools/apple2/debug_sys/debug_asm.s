; debug_asm.s — ROM + MLI helpers for DEBUG.SYSTEM.
;
; DEBUG.SYSTEM is the standalone diagnostic the boot launcher chains from its
; menu (the screen used to be drawn in-process; it moved out so its code stopped
; competing with the launcher's near-zero BSS headroom, which now funds the II+
; editor's typing fast path). This is a small, self-contained copy of the
; launcher's ROM wrappers plus a handful of read-only MLI verbs the diagnostic
; needs (ON_LINE / READ_BLOCK / GET_TIME / GET_PREFIX / GET_FILE_INFO) and the
; OPEN/READ/CLOSE chain bouncer back to SWIFTII.SYSTEM. It does NOT link
; tools/apple2/boot_launcher/boot_launcher_asm.s (that object's whole-object link would
; drag in the probes, all the MLI verbs, and ~16 globals it doesn't need).

        .export _a_home
        .export _a_vtab
        .export _a_cout
        .export _a_print_hex
        .export _a_kbd_wait
        .export _a_chain_launcher
        .export _a_cpu_is_65c02
        .export _a_aux_text_present
        .export _a_mli_online
        .export _a_mli_read_block
        .export _a_mli_get_prefix

        .import _g_online               ; ON_LINE result buffer (256 B)
        .import _g_rb_unit              ; READ_BLOCK unit_num (DSSS0000)
        .import _g_rb_block             ; READ_BLOCK logical block (16-bit)
        .import _g_prefix               ; GET_PREFIX result (len-prefixed)

COUT            = $FDED
HOME            = $FC58
TABV            = $FB5B         ; A -> CV, recompute text base address
CH              = $24           ; 40-col cursor column
KBD             = $C000
KBDSTRB         = $C010
MLI             = $BF00
MLI_OPEN        = $C8
MLI_READ        = $CA
MLI_CLOSE       = $CC
MLI_ONLINE      = $C5
MLI_READ_BLOCK  = $80
MLI_GET_PREFIX  = $C7
IOBUF           = $1C00         ; MLI file I/O buffer (4 pages, $1C00-$1FFF)
DIRBLK          = $0800         ; READ_BLOCK target (one 512 B ProDOS block)
LC_BANK1_RO     = $C088         ; read RAM bank 1, write protect (MLI idle state)
ZP_BOUNCER      = $B0           ; .. $BE (15 B: JSR MLI READ + CLOSE + JMP $2000)
ZP_READ_PARMS   = $C0           ; .. $C7 (8 B)
ZP_CLOSE_PARMS  = $C8           ; .. $C9 (2 B)

        .code

; ------------------------------------------------------------
; void a_home(void) — ROM clear + home (40-col text page).
; ------------------------------------------------------------
_a_home:
        jmp     HOME

; ------------------------------------------------------------
; void a_vtab(uint8_t row) — move the text cursor to (col 0, row) via the
; monitor TABV, then zero CH so the next COUT lands at column 0. Lets the page
; legend pin to a fixed bottom row (40-col text page).
; ------------------------------------------------------------
_a_vtab:
        jsr     TABV
        lda     #0
        sta     CH
        rts

; ------------------------------------------------------------
; void a_cout(uint8_t c) — print c via ROM COUT, high bit forced on.
; ------------------------------------------------------------
_a_cout:
        ora     #$80
        jmp     COUT

; ------------------------------------------------------------
; void a_print_hex(uint8_t b) — print b as two ASCII hex digits via COUT.
; ------------------------------------------------------------
_a_print_hex:
        pha
        lsr
        lsr
        lsr
        lsr
        jsr     hexd
        pla
        and     #$0F
hexd:
        ora     #$B0            ; '0' | $80
        cmp     #$BA            ; one past '9' | $80
        bcc     :+
        adc     #6              ; bridge to 'A' | $80 across the gap
:       jmp     COUT

; ------------------------------------------------------------
; uint8_t a_kbd_wait(void) — block until a key, ack the strobe, return char&$7F.
; ------------------------------------------------------------
_a_kbd_wait:
        lda     KBD
        bpl     _a_kbd_wait
        sta     KBDSTRB         ; ack strobe
        and     #$7F
        ldx     #0
        rts

; ------------------------------------------------------------
; uint8_t a_cpu_is_65c02(void) — 1 if a 65C02 (enhanced //e / accelerator),
; 0 on an NMOS 6502. $1A is INC A on the 65C02 but a one-byte NOP on the 6502,
; so the accumulator ends at 1 only on the CMOS part.
; ------------------------------------------------------------
_a_cpu_is_65c02:
        lda     #0
        .byte   $1A             ; INC A (65C02) / NOP (6502)
        ldx     #0
        rts

; ------------------------------------------------------------
; uint8_t a_aux_text_present(void) — 1 if the //e auxiliary text-page RAM (an
; 80-column card, plain or extended) is present, 0 if not. CALL ON //e ONLY:
; on a II+ the $C001/$C055 switches mean other things ($C055 flips the DISPLAY to
; page 2) and $0478 is ordinary page-1 RAM, so it would false-positive — the C
; side gates this behind the FBB3 //e check.
;
; Banks ONLY the text page $0400-$07FF to aux via 80STORE + PAGE2 (so instruction
; fetch from our $2000 code is unaffected), then writes two patterns to a
; non-displayed screen hole ($0478) and reads them back. Real RAM echoes both;
; an absent aux card returns floating-bus values that almost never match. The
; hole's prior value is saved/restored and the switches are returned to normal.
; ------------------------------------------------------------
ST80ON          = $C001         ; 80STORE on  (PAGE2 then banks the text page)
ST80OFF         = $C000         ; 80STORE off
PAGE2ON         = $C055         ; text page -> aux (with 80STORE on)
PAGE2OFF        = $C054         ; text page -> main
AUXHOLE         = $0478         ; a text-page screen hole (never displayed)
_a_aux_text_present:
        sta     ST80ON
        sta     PAGE2ON         ; $0400-$07FF now the aux text page
        ldx     AUXHOLE         ; save aux[$0478]
        lda     #$A5
        sta     AUXHOLE
        cmp     AUXHOLE         ; echoed?
        bne     @no
        lda     #$5A
        sta     AUXHOLE
        cmp     AUXHOLE         ; echoed the 2nd pattern too?
        bne     @no
        stx     AUXHOLE         ; restore
        sta     PAGE2OFF
        sta     ST80OFF
        lda     #1
        ldx     #0
        rts
@no:
        stx     AUXHOLE         ; restore
        sta     PAGE2OFF
        sta     ST80OFF
        lda     #0
        ldx     #0
        rts

; ------------------------------------------------------------
; uint8_t a_mli_online(void) — ON_LINE all units into _g_online (256 B). Each
; 16-byte record: byte 0 = (unit_num << 4) | name_length; bytes 1.. = volume
; name. A zero name_length means that slot has no mounted volume.
; ------------------------------------------------------------
_a_mli_online:
        jsr     MLI
        .byte   MLI_ONLINE
        .word   on_parms
        rts

; ------------------------------------------------------------
; uint8_t a_mli_read_block(void) — READ_BLOCK _g_rb_block from device _g_rb_unit
; into DIRBLK ($0800). Returns the MLI error (0 = ok). Block reads are
; device-level, so they work regardless of the current prefix / open refnum.
; ------------------------------------------------------------
_a_mli_read_block:
        lda     _g_rb_unit
        sta     rb_parms+1
        lda     _g_rb_block
        sta     rb_parms+4
        lda     _g_rb_block+1
        sta     rb_parms+5
        jsr     MLI
        .byte   MLI_READ_BLOCK
        .word   rb_parms
        rts

; ------------------------------------------------------------
; uint8_t a_mli_get_prefix(void) — GET_PREFIX into _g_prefix (len-prefixed).
; ------------------------------------------------------------
_a_mli_get_prefix:
        jsr     MLI
        .byte   MLI_GET_PREFIX
        .word   gp_parms
        rts

; ------------------------------------------------------------
; void a_chain_launcher(void)
; OPEN SWIFTII.SYSTEM, then READ it to $2000 via a ZP-resident bouncer (the READ
; overwrites our own $2000 code, so READ/CLOSE/JMP must run from ZP), CLOSE, and
; JMP $2000. Returns (rts) only if the OPEN fails. The current ProDOS prefix is
; the boot volume the launcher set, so the relative name resolves there.
; ------------------------------------------------------------
_a_chain_launcher:
        jsr     MLI
        .byte   MLI_OPEN
        .word   open_parms
        bne     chain_fail

        ldx     #(bouncer_end - bouncer_tpl - 1)
:       lda     bouncer_tpl,x
        sta     ZP_BOUNCER,x
        dex
        bpl     :-

        ldx     #(read_end - read_tpl - 1)
:       lda     read_tpl,x
        sta     ZP_READ_PARMS,x
        dex
        bpl     :-

        ldx     #(close_end - close_tpl - 1)
:       lda     close_tpl,x
        sta     ZP_CLOSE_PARMS,x
        dex
        bpl     :-

        lda     open_parms+5        ; OPEN wrote the refnum here
        sta     ZP_READ_PARMS+1
        sta     ZP_CLOSE_PARMS+1

        lda     LC_BANK1_RO         ; MLI expects bank-1 RAM read on entry
        jmp     ZP_BOUNCER
chain_fail:
        rts

; ------------------------------------------------------------
        .rodata

; Bouncer template (copied to ZP_BOUNCER). The inline .word operands point at
; the ZP-resident param copies because by the time it runs the READ has wiped
; $2000+ and these templates are gone.
bouncer_tpl:
        jsr     MLI
        .byte   MLI_READ
        .word   ZP_READ_PARMS
        jsr     MLI
        .byte   MLI_CLOSE
        .word   ZP_CLOSE_PARMS
        jmp     $2000
bouncer_end:

read_tpl:
        .byte   4               ; pcount
        .byte   0               ; refnum (patched in ZP)
        .word   $2000           ; data buffer
        .word   $9F00           ; request count (full SYS file ceiling)
        .word   0               ; xfer count (out)
read_end:

close_tpl:
        .byte   1               ; pcount
        .byte   0               ; refnum (patched in ZP)
close_end:

swiftii_path:
        .byte   14
        .byte   "SWIFTII.SYSTEM"

; ------------------------------------------------------------
        .data

; OPEN param block — MLI writes the refnum into open_parms+5, so this lives in
; writable memory (not RODATA).
open_parms:
        .byte   3               ; pcount
        .word   swiftii_path    ; pathname
        .word   IOBUF           ; io_buffer
        .byte   0               ; refnum (out)

; ON_LINE: count=2, unit_num=0 (all online volumes), 256-byte result buffer.
on_parms:
        .byte   2
        .byte   0
        .word   _g_online

; READ_BLOCK: count=3, unit_num (patched), data buffer (DIRBLK), block (patched).
rb_parms:
        .byte   3
        .byte   0               ; unit_num (patched from _g_rb_unit)
        .word   DIRBLK          ; data buffer ($0800)
        .word   0               ; block_num (patched from _g_rb_block)

; GET_PREFIX: count=1, data buffer (>= 64 B, len-prefixed result).
gp_parms:
        .byte   1
        .word   _g_prefix
