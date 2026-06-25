; tools/apple2/boot_launcher/boot_launcher_asm.s — low-level helpers for boot_launcher.c.
;
; Exposes C-callable functions in cc65's __fastcall__ convention:
;   - byte arg in A on entry
;   - byte return value in A
;   - X and Y are caller-clobbered
;
; Talks directly to ROM ($FC58 HOME, $FDED COUT, $FCA8 WAIT), to the
; Apple II/IIe soft switches (LC bank/mode, aux RAM, slot card I/O),
; and to ProDOS MLI ($BF00). The probes and ZP bouncer live here
; because expressing them in C would either be unsafe (compiler can
; legally fold/reorder volatile reads, which breaks the LC two-read
; prewrite) or impossible (the bouncer is a literal byte sequence
; copied into ZP, JMPed into, and overwritten by MLI READ).

.export _a_home
.export _a_cout
.export _a_cout_raw
.export _a_vtab
.export _a_wait
.export _a_kbd
.export _a_print_hex
.export _a_probe_saturn
.export _a_probe_aux
.export _a_mli_open
.export _a_mli_read_startup
.export _a_install_and_chain
.export _a_install_and_chain_swiftsat
.export _a_install_and_chain_swiftaux
; File-manager mini-UI MLI verbs (boot-launcher only; called from
; C in the same LC=ROM-read context as a_mli_open/a_mli_read_startup —
; the MLI dispatcher at $BF00 manages its own banking, so no LC switch).
.export _a_mli_get_prefix
.export _a_mli_set_prefix
.export _a_mli_read_dirblk
.export _a_mli_close
.export _a_mli_destroy
.export _a_mli_rename
.export _a_mli_create_dir
; Resume note ("LASTRUN") verbs.
.export _a_mli_destroy_note
.export _a_mli_create_note
.export _a_mli_write_note
.export _a_mli_read_note
; Volume picker (ON_LINE).
.export _a_mli_online
.ifdef LITE_IIE
; Volume free/total space (READ_BLOCK) — //e launcher only.
.export _a_mli_read_block
.endif

.import _g_saturn_slot
.import _g_aux_found
.import _g_open_pathname
.import _g_open_refnum
.import _g_slot0_readback
.import _g_slot_readback
; File-manager path buffers (length-prefixed, in C BSS).
.import _g_prefix               ; current ProDOS prefix (GET_PREFIX out)
.import _g_path                 ; action target path / SET_PREFIX in / etc.
.import _g_path2                ; RENAME new-name
.import _g_note                 ; resume-note serialization buffer
.import _g_notepath             ; absolute path of the LASTRUN note file
.import _g_note_len             ; bytes to WRITE from _g_note (uint16)
.import _g_online               ; ON_LINE result buffer (256 B, 16-byte records)
.import _g_width80; 0 = 40-col, 1 = 80-col active
.ifdef LITE_IIE
.import _g_rb_unit              ; READ_BLOCK unit_num (DSSS0000)
.import _g_rb_block             ; READ_BLOCK block number (uint16)
.endif

; ============================================================
; Equates
; ============================================================

; ROM
COUT            = $FDED
HOME            = $FC58
WAIT            = $FCA8
TABV            = $FB5B         ; A → CV, recompute BASL/BASH
OURCH           = $057B         ; 80-col firmware cursor column (//e)
FF              = $8C           ; Ctrl-L | $80: firmware clear-screen + home

; ProDOS MLI
MLI             = $BF00
MLI_CREATE      = $C0
MLI_DESTROY     = $C1
MLI_RENAME      = $C2
MLI_SET_PREFIX  = $C6
MLI_GET_PREFIX  = $C7
MLI_ONLINE      = $C5
MLI_OPEN        = $C8
MLI_READ        = $CA
MLI_WRITE       = $CB
MLI_CLOSE       = $CC
MLI_READ_BLOCK  = $80

; Directory-block read buffer. $0800-$09FF is free main RAM
; while the file browser runs (it is the chain's XLC staging scratch
; only later, during the interpreter chain). A 512 B directory block
; lands here; boot_launcher.c parses it.
DIRBLK          = $0800

; MLI I/O buffer (4 pages, page-aligned, below the $2000 load area)
IOBUF           = $1C00

; Language card switches
LC_BANK2_RO     = $C080         ; read RAM bank 2, write protect
LC_BANK2_RW     = $C081         ; read RAM bank 2, write enable (TWO reads required)
LC_BANK1_RO     = $C088         ; read RAM bank 1, write protect (MLI idle)
LC_ROM_RO       = $C082         ; read ROM, write protect

; Keyboard
KBD             = $C000
KBDSTRB         = $C010

; Our ZP usage. cc65's ZEROPAGE lives at $80-$99; we own $9A-$FF (the
; ProDOS app range minus what MLI claims). Pick safe slots well above
; cc65's range so any cc65 codegen we're not aware of can't collide.
SAVED_LC_BYTE   = $A8
SLOT_PTR        = $AA           ; .. $AB
; --- Lite/legacy-extras path (single MLI READ) ---
ZP_BOUNCER      = $B0           ; .. $BE (15 B: JSR MLI READ + CLOSE + JMP $2000)
ZP_READ_PARMS   = $C0           ; .. $C7 (8 B)
ZP_CLOSE_PARMS  = $C8           ; .. $C9 (2 B)
; --- SWIFTSAT path (2-chunk packed file) ---
; Header buffer SX_HEADER_BUF in MAIN RAM at $1B00 (just below the
; ProDOS MLI I/O buffer at $1C00-$1FFF). Per ProDOS 8 Tech Ref
; § 6.5: MLI READ's data buffer must be in main memory, not ZP.
; Verified empirically 2026-05-28: a buffer at ZP $AC-$AF AND at
; ZP $CA-$CD both fail (the buffer is left holding the pre-call
; values, the MLI READ silently doesn't deliver the bytes). $1B00 is
; in the safe range $0800-$1BFF below the boot launcher's $2000+
; image, untouched by all MLI READs to $2000+ or $D000+.
SX_HEADER_BUF   = $1B00         ; 4 B: main_lo,_hi, xlc_lo,_hi
; Saturn slot byte stashed by a_install_and_chain_swiftsat at $1B04.
; LOADER reads this to patch its own Saturn switches; SwiftII's
; xlc_init reads it at main() entry to patch the runtime trampoline.
; Survives the MLI READ of main → $2000+ because $1B04 sits below the
; load region.
SX_SAT_SLOT     = $1B04         ; 1 B: g_saturn_slot at chain time
; XLC chunked-staging ZP slots (used in commit 3+ when XLC has content):
ZP_SX_SRC       = $B2           ; .. $B3 (16-bit memcpy src ptr)
ZP_SX_DST       = $B4           ; .. $B5 (16-bit memcpy dst ptr)
ZP_SX_REMAIN    = $B6           ; .. $B7 (16-bit bytes left in XLC)
ZP_SX_CHUNK     = $B8           ; .. $B9 (16-bit chunk size this iter)
; The lite-path ZP_BOUNCER ($B0-$BE) overlaps with ZP_SX_SRC..CHUNK.
; That's fine because the two chain paths are mutually exclusive.

; XLC chunked-staging scratch buffer in main RAM. MLI READ deposits
; each XLC chunk here; LOADER then memcpys to Saturn bank 1 at
; $D000+offset (or, for SWIFTAUX, AUXMOVEs to aux main RAM).
SX_XLC_SCRATCH  = $0800
; XLC staging chunk size. (2026-05-30) implemented the chunked
; loop the commit-3a single-shot path deferred — XLC outgrew the 4864 B
; scratch once GR landed (5,696 B). Each iteration reads up to
; SX_XLC_CHUNK bytes into the scratch ($0800) and memcpys/AUXMOVEs it on.
;
; The chunk MUST keep the scratch below $0C00: (run-from-disk)
; reserves $0C00..$13FF (STAGED_SRC_ADDR, FILE_SRC_SIZE = 2048 B) for the
; program the boot launcher staged for the interpreter, and the extras chains
; run that staged program too. A $1000 chunk reached $17FF and silently
; clobbered the staged source — that surfaced the moment the file browser
; could launch a .swift file on SWIFTSAT/SWIFTAUX (lite reads main
; straight to $2000 and never touches this scratch, so it was unaffected).
; $0400 pins the scratch to $0800..$0BFF, exactly the free gap below the
; staged-source region; the cost is a few more MLI READ iterations during
; the one-time extras boot. The only size ceiling remains the $3000
; Saturn-bank-1 budget, which pack_swiftsat.py enforces at build time.
SX_XLC_CHUNK    = $0400         ; 1024 B/iter; keeps scratch $0800..$0BFF

; ============================================================
.segment "CODE"

; ------------------------------------------------------------
; void a_home(void)
; Clear text screen and home cursor. 40-col: tail-call ROM HOME.
; 80-col: ROM HOME clears only the Apple 40-col page, which
; the //e 80-col firmware does not display — so emit a
; form-feed (Ctrl-L) through COUT, which dispatches via CSW into the active
; firmware's clear-screen+home handler.
; ------------------------------------------------------------
_a_home:
        lda     _g_width80
        beq     @rom
        lda     #FF             ; Ctrl-L | $80 -> COUT -> CSW -> firmware clear
        jmp     COUT
@rom:   jmp     HOME

; ------------------------------------------------------------
; void a_cout(uint8_t c)
; Print a 7-bit char to the screen via ROM COUT, OR-ing $80 in
; so COUT sees the Apple II high-bit-set convention.
; A = c on entry (cc65 fastcall single-byte arg).
; ------------------------------------------------------------
_a_cout:
        ora     #$80
        jmp     COUT

; ------------------------------------------------------------
; void a_cout_raw(uint8_t c)
; Print a char via ROM COUT WITHOUT forcing the high bit, so a value
; in $00-$3F lands on screen as an inverse glyph. Used to draw the
; blinking inverse-block text cursor ($20 = inverse space).
; ------------------------------------------------------------
_a_cout_raw:
        jmp     COUT

; ------------------------------------------------------------
; void a_vtab(uint8_t row)
; Move text cursor to (col 0, row 0..23). Uses monitor TABV so CV (which both
; the 40-col monitor AND the 80-col firmware read for the row) and BASL/BASH
; stay coherent for subsequent COUT calls; zeroes CH (40-col column). In 80-col
; the firmware's column is OURCH ($057B), not CH, so zero that too — otherwise
; the next character lands at the stale 80-col column.
; ------------------------------------------------------------
_a_vtab:
        jsr     TABV            ; A → CV, recompute BASL/BASH
        lda     #0
        sta     $24             ; CH = 0 (40-col column)
        ldx     _g_width80
        beq     @done
        sta     OURCH           ; 80-col column = 0
@done:  rts

; ------------------------------------------------------------
; void a_wait(void)
; Block ~0.16 s on a 1 MHz Apple II via ROM WAIT($FF).
; ------------------------------------------------------------
_a_wait:
        lda     #$FF
        jmp     WAIT

; ------------------------------------------------------------
; uint8_t a_kbd(void)
; Non-blocking keyboard poll. Returns char|$80 in A if a key is
; pending (and acks the strobe via $C010); else 0 in A.
; ------------------------------------------------------------
_a_kbd:
        lda     KBD
        bmi     :+
        lda     #0
        rts
:       ldy     KBDSTRB         ; ack strobe — Y is clobberable
        rts

; ------------------------------------------------------------
; void a_print_hex(uint8_t b)
; Print A as two ASCII hex digits via COUT.
; ------------------------------------------------------------
_a_print_hex:
        pha
        lsr
        lsr
        lsr
        lsr
        jsr     hex_digit
        pla
        and     #$0F
hex_digit:
        ora     #$B0            ; '0' | $80
        cmp     #$BA            ; one past '9' | $80
        bcc     :+
        adc     #6              ; bridge to 'A' | $80 across the gap
:       jmp     COUT

; (a_read_fbb3 / a_read_machid removed: their only caller was the
; launcher's in-process Debug screen, which moved into the standalone
; DEBUG.SYSTEM — that program reads $FBB3 / $BF98 directly in C.)

; ------------------------------------------------------------
; void a_probe_saturn(void)
; Sets _g_saturn_slot to 0..7 (slot where Saturn found) or $FF.
;
; Strategy: slot-0 multi-bank verification (Saturn-as-LC vs 16K LC)
; followed by simple R+W tests on slots 1..7. Main LC is held in
; bank-2 RAM / write-protect throughout so spurious writes from a
; no-card slot drop instead of trampling MLI in bank 1.
;
; Side effect: leaves LC in ROM-read mode so subsequent COUT calls
; (from C, going through _a_cout) work. The pre-MLI a_install_and_
; chain switches LC to bank-1 RAM read.
; ------------------------------------------------------------
_a_probe_saturn:
        lda     #$FF
        sta     _g_saturn_slot

        lda     LC_BANK2_RO
        lda     LC_BANK2_RO

        ; --- Slot 0 multi-bank probe ---
        ; On a Saturn-as-LC, $C086 selects bank 3 and $C08C selects
        ; bank 5 (independent banks of 16 KB each). On a 16K LC,
        ; $C086 and $C08C are mode mirrors (read-ROM and read-bank-1
        ; respectively), not bank selectors, so the second write to
        ; $D000 overwrites the first and the readback shows the
        ; *second* sentinel — correctly distinguishing Saturn from
        ; a regular 16K LC.

        lda     $D000
        sta     SAVED_LC_BYTE

        lda     $C086           ; Saturn bank 3 / 16K LC read-ROM
        lda     $C083           ; R+W bank 2 prewrite (1st of 2)
        lda     $C083           ; R+W bank 2 enabled
        lda     #$A5
        sta     $D000

        lda     $C08C           ; Saturn bank 5 / 16K LC read-bank-1
        lda     $C083
        lda     $C083
        lda     #$5A
        sta     $D000

        lda     $C086           ; back to Saturn bank 3
        lda     $C083
        lda     $C083
        lda     $D000
        sta     _g_slot0_readback   ; debug: what bank-3 read returned
        cmp     #$A5
        bne     no_slot0

        lda     #0
        sta     _g_saturn_slot
        lda     SAVED_LC_BYTE
        sta     $D000
        jmp     saturn_done

no_slot0:
        lda     SAVED_LC_BYTE
        sta     $D000

        ; Switch main LC to ROM-read mode for the slot 1..7 probe loop.
        ; If we left main LC in bank-2 RAM read, both the on-board /
        ; 16K LC RAM *and* a Saturn card in slot N would drive the
        ; data bus at $D000 simultaneously when Saturn is enabled —
        ; on Mariani's IIe the main LC wins that arbitration and any
        ; Saturn writes become invisible to reads, so the probe never
        ; detects the card. ROM-read mode takes main LC off the bus
        ; so only Saturn drives reads at $D000+ once we enable it.
        lda     LC_ROM_RO

        ; --- Slots 1..7 simple R+W probe ---
        ; Slot N's I/O lives at $C080 + N*16 (slot 1 at $C090, slot 7
        ; at $C0F0). An earlier version used $C0(N*16) here and ended
        ; up hitting system soft switches ($C053 mixed video, etc.) —
        ; see docs/contributing/LESSONS.md 2026-05-26.
        ldx     #7
slot_loop:
        txa
        asl
        asl
        asl
        asl                     ; A = N * 16
        ora     #$80            ; → slot N's $C0(N+8)*16 base
        sta     SLOT_PTR
        lda     #$C0
        sta     SLOT_PTR+1

        ldy     #3
        lda     (SLOT_PTR),y    ; prewrite 1 of $C0N3
        lda     (SLOT_PTR),y    ; prewrite 2 — R+W enabled if Saturn-class

        lda     $D000
        sta     SAVED_LC_BYTE
        eor     #$FF
        sta     $D000
        ; Debug: capture the readback at $D000 each iteration so the
        ; user can see what reads actually returned during the probe.
        ; After the loop exits, _g_slot_readback holds the byte from
        ; the *last* slot tested (slot 1 if no match, or the matched
        ; slot if one was found). Y is fastcall-clobberable here.
        ldy     $D000
        sty     _g_slot_readback
        cmp     _g_slot_readback    ; A (inverted write) vs readback
        bne     no_card

        stx     _g_saturn_slot
        lda     SAVED_LC_BYTE
        sta     $D000
        jmp     saturn_done

no_card:
        dex
        bne     slot_loop

saturn_done:
        ; Re-select Saturn bank 1 (LDA $C084 — a no-op on 16K LC /
        ; no LC, where it's a $C080 mirror) so MLI can later read its
        ; body from Saturn bank 1, then put LC in ROM-read mode for
        ; subsequent COUT calls.
        lda     $C084
        lda     LC_ROM_RO
        rts

; ------------------------------------------------------------
; void a_probe_aux(void)
; Sets _g_aux_found to 1 iff this machine has a real 64K (extended)
; aux RAM block — i.e. SWIFTAUX's copy-down park ($2000-$77FF aux)
; will fit. Else 0 (route to lite).
;
; Two stages:
;   1. MACHID gate. ProDOS's MACHID ($BF98) bit 4 is set when an
;      80-column card is present. If it's clear there's no aux at
;      all (and on a II+ the //e $Cxxx ROM that holds AUXMOVE isn't
;      even there) -> g_aux_found = 0, done. This gate also keeps the
;      AUXMOVE call below off non-//e machines.
;   2. 64K confirmation. Bit 4 is *also* set for a *basic* 80-col card
;      (1K aux: the $0400-$07FF text page only, no $2000 block), which
;      would crash SWIFTAUX (its park can't load). Distinguish basic
;      from extended with an AUXMOVE round-trip at the park base
;      (aux $2000): write a byte to aux $2000 and read it back via the
;      same ROM AUXMOVE the park loader uses (which DOES work on
;      Mariani, unlike the raw RAMRD soft switch — see LESSONS
;      2026-05-26). Two patterns ($A5 then $5A) guard against a
;      floating bus echoing a single value. Both survive only if real
;      RAM lives at aux $2000 -> extended -> g_aux_found = 1.
;
; Clobbers aux $2000 (the park loader overwrites it on the SWIFTAUX
; path; the lite path never touches it) and the monitor A1/A2/A4
; ($3C-$43) + PB_SRC/PB_DST scratch.
; ------------------------------------------------------------
PB_AUXMOVE = $C311              ; ROM block move (//e internal $Cxxx ROM)
PB_SETSLOT = $C006              ; SLOTCXROM: slot-card ROM at $Cxxx
PB_SETINT  = $C007              ; INTCXROM: internal ROM at $Cxxx (for AUXMOVE)
PB_A1L     = $3C                ; monitor A1 (source start)
PB_A2L     = $3E                ; monitor A2 (source end, inclusive)
PB_A4L     = $42                ; monitor A4 (destination start)
PB_SRC     = $AC                ; main scratch: sentinel out (launcher-owned ZP)
PB_DST     = $AD                ; main scratch: read-back in
PB_AUX_LO  = $00                ; aux $2000 = SWIFTAUX park base
PB_AUX_HI  = $20

_a_probe_aux:
        lda     #0
        sta     _g_aux_found
        lda     $BF98           ; MACHID
        and     #$10            ; bit 4 = 80-col card present
        beq     ap_done         ; no card -> no aux (and not a //e) -> 0
        ; Card present: basic (1K) or extended (64K)? Round-trip 2 patterns
        ; through aux $2000; only real RAM there round-trips both.
        lda     #$A5
        jsr     ap_roundtrip
        cmp     #$A5
        bne     ap_done         ; mismatch -> basic/none -> stays 0
        lda     #$5A
        jsr     ap_roundtrip
        cmp     #$5A
        bne     ap_done
        lda     #1              ; both survived -> real 64K aux present
        sta     _g_aux_found
ap_done:
        rts

; A = sentinel byte in. Writes it to aux $2000 and reads aux $2000 back,
; returning the read byte in A. Each AUXMOVE runs with INTCXROM in (so
; $C311 is the ROM routine) and restores SLOTCXROM for the next MLI.
ap_roundtrip:
        sta     PB_SRC
        ; --- main PB_SRC -> aux $2000 (1 byte; carry set = main->aux) ---
        lda     #<PB_SRC
        sta     PB_A1L
        lda     #>PB_SRC
        sta     PB_A1L+1
        lda     #<PB_SRC
        sta     PB_A2L
        lda     #>PB_SRC
        sta     PB_A2L+1
        lda     #PB_AUX_LO
        sta     PB_A4L
        lda     #PB_AUX_HI
        sta     PB_A4L+1
        sta     PB_SETINT
        sec                     ; main -> aux
        jsr     PB_AUXMOVE
        sta     PB_SETSLOT      ; restore slot ROM
        ; --- aux $2000 -> main PB_DST (1 byte; carry clear = aux->main) ---
        lda     #PB_AUX_LO
        sta     PB_A1L
        lda     #PB_AUX_HI
        sta     PB_A1L+1
        lda     #PB_AUX_LO
        sta     PB_A2L
        lda     #PB_AUX_HI
        sta     PB_A2L+1
        lda     #<PB_DST
        sta     PB_A4L
        lda     #>PB_DST
        sta     PB_A4L+1
        sta     PB_SETINT
        clc                     ; aux -> main
        jsr     PB_AUXMOVE
        sta     PB_SETSLOT
        lda     PB_DST
        rts

; ------------------------------------------------------------
; uint8_t a_mli_open(void)
; MLI OPEN of _g_open_pathname into IOBUF. Returns the MLI error
; code (0 = success) in A; on success writes the refnum into
; _g_open_refnum.
; ------------------------------------------------------------
_a_mli_open:
        lda     _g_open_pathname
        sta     open_parms+1
        lda     _g_open_pathname+1
        sta     open_parms+2

        jsr     MLI
        .byte   MLI_OPEN
        .word   open_parms
        bne     open_err

        lda     open_parms+5    ; refnum field
        sta     _g_open_refnum
        lda     #0
open_err:
        rts

; ------------------------------------------------------------
; void a_install_and_chain(void)
; Copies the MLI READ + CLOSE bouncer (and its param blocks) into
; ZP so they survive the READ that overwrites $2000+. Patches the
; OPEN refnum into the ZP-resident READ + CLOSE blocks. Switches
; LC to bank-1 RAM read (MLI's expected state on entry). JMPs to
; the bouncer at ZP_BOUNCER. Never returns.
; ------------------------------------------------------------
_a_install_and_chain:
        ldx     #(bouncer_tpl_end - bouncer_tpl - 1)
:       lda     bouncer_tpl,x
        sta     ZP_BOUNCER,x
        dex
        bpl     :-

        ldx     #(read_parms_end - read_parms_tpl - 1)
:       lda     read_parms_tpl,x
        sta     ZP_READ_PARMS,x
        dex
        bpl     :-

        ldx     #(close_parms_end - close_parms_tpl - 1)
:       lda     close_parms_tpl,x
        sta     ZP_CLOSE_PARMS,x
        dex
        bpl     :-

        ; Patch refnum into the ZP copies (the templates above had
        ; refnum = 0; we know it now from _g_open_refnum).
        lda     _g_open_refnum
        sta     ZP_READ_PARMS+1
        sta     ZP_CLOSE_PARMS+1

        lda     LC_BANK1_RO     ; MLI expects bank-1 RAM read on entry
        jmp     ZP_BOUNCER

; ============================================================
.segment "RODATA"

; Bouncer template — copied to ZP_BOUNCER ($B0). The inline .word
; operands reference the ZP-resident param-block copies because by
; the time the bouncer runs, MLI READ has overwritten $2000+ and
; the templates here are gone.
bouncer_tpl:
        jsr     MLI
        .byte   MLI_READ
        .word   ZP_READ_PARMS
        jsr     MLI
        .byte   MLI_CLOSE
        .word   ZP_CLOSE_PARMS
        jmp     $2000
bouncer_tpl_end:

; READ param-block template (copied to ZP_READ_PARMS at chain time).
; Refnum (byte index 1) is patched from _g_open_refnum at install.
read_parms_tpl:
        .byte   4               ; pcount
        .byte   0               ; refnum (patched)
        .word   $2000           ; data buffer
        .word   $9F00           ; request count (full SYS file ceiling)
        .word   0               ; xfer count (out)
read_parms_end:

; CLOSE param-block template (copied to ZP_CLOSE_PARMS at chain time).
close_parms_tpl:
        .byte   1               ; pcount
        .byte   0               ; refnum (patched)
close_parms_end:

; ============================================================
; SWIFTSAT chain
; ============================================================
;
; Loads SWIFTSAT.SYSTEM (4-byte header + main + XLC per design doc
; 012). The main image contains cc65's standard LC bytes (cc65's
; crt0.s memcpys them to built-in LC bank 2 at startup). The XLC
; image is the extras-only code that lives in Saturn bank
; 1; the LOADER copies it there via a main-RAM scratch buffer
; ($0800) so MLI's body in built-in LC bank 1 stays intact.
;
; Sequence:
;   1. Install fn: stash _g_saturn_slot at SX_SAT_SLOT ($1B04).
;   2. Install fn: MLI READ 4-byte header into SX_HEADER_BUF.
;   3. Install fn: copy LOADER segment from MAIN to $0300.
;   4. Install fn: JMP $0300.
;   5. Loader (step 0): read SX_SAT_SLOT, patch its Saturn
;      bank-select / R-RAM-WE switches.
;   6. Loader (step 1): MLI READ main → $2000.
;      7. Loader (step 2): if xlc_size > 0, stage it in chunks of up
;      to SX_XLC_CHUNK ($0400) B — per chunk: MLI READ → $0800
;      scratch, switch built-in LC to ROM-read (cede $D000+ bus to
;      Saturn on Mariani //e), arm Saturn bank 1 R-RAM-WE, memcpy
;      scratch → running $D000+ dst, restore built-in LC to bank-1
;      R/O for the next READ. Loops until all xlc_size bytes staged.
; (replaced an earlier single-shot READ + $FAA9 halt
;      once XLC outgrew the 4864 B scratch.)
;   8. Loader (step 3): MLI CLOSE (MLI body intact throughout — no
;      leaked refnum).
;   9. Loader (step 4): built-in LC → ROM-read (cc65 crt0 expects
;      ProDOS-style handoff state), JMP $2000.
;
; ZP layout: see ZP_SX_* equates above. ZP_READ_PARMS and
; ZP_CLOSE_PARMS at $C0/$C8 are reused; the lite-path ZP_BOUNCER at
; $B0-$BE overlaps with the XLC chunked-staging ZP slots, but the
; two chain paths are mutually exclusive (only one per boot).

.segment "CODE"

.import __LOADER_LOAD__, __LOADER_SIZE__, __LOADER_RUN__

; ------------------------------------------------------------
; void a_install_and_chain_swiftsat(void)
; SWIFTSAT-specific entry. Caller has already done MLI OPEN
; (g_open_refnum set). This fn does the 6-byte header READ,
; copies the LOADER segment to $0300, patches refnums into the
; ZP MLI param blocks, switches LC to MLI's expected bank-1 R/O
; state, and JMPs to the loader. Never returns.
; ------------------------------------------------------------
_a_install_and_chain_swiftsat:
        ; --- Stash Saturn slot for LOADER + SwiftII to read ---
        ; SWIFTSAT only runs when probe_saturn detected a card, so
        ; _g_saturn_slot is in [0..7]. SX_SAT_SLOT survives MLI READ
        ; of main → $2000+ (it sits at $1B04, below the load region).
        ; LOADER reads it to patch its own Saturn switches; SwiftII's
        ; xlc_init reads it at main() entry to patch the call_xlc
        ; trampoline.
        lda     _g_saturn_slot
        sta     SX_SAT_SLOT

        ; --- MLI READ 4-byte header → SX_HEADER_BUF ---
        ; Build read params inline (no template needed for one-shot use).
        lda     _g_open_refnum
        sta     ZP_READ_PARMS+1
        lda     #<SX_HEADER_BUF
        sta     ZP_READ_PARMS+2
        lda     #>SX_HEADER_BUF
        sta     ZP_READ_PARMS+3
        lda     #4                      ; request count: 4-byte header
        sta     ZP_READ_PARMS+4
        lda     #0
        sta     ZP_READ_PARMS+5
        lda     #4                      ; pcount
        sta     ZP_READ_PARMS+0

        lda     LC_BANK1_RO             ; MLI expects bank-1 RAM read
        jsr     MLI
        .byte   MLI_READ
        .word   ZP_READ_PARMS
        ; (Error handling deferred — if MLI fails here, the loader's
        ; subsequent operations will likely BRK in a way the boot
        ; selector's "MLI ERROR" path doesn't catch.)

        ; --- Patch refnum into ZP_CLOSE_PARMS (for the loader's CLOSE) ---
        lda     _g_open_refnum
        sta     ZP_CLOSE_PARMS+1
        lda     #1                      ; pcount
        sta     ZP_CLOSE_PARMS+0

        ; --- Copy LOADER segment from MAIN-load-addr to $0300 ---
        ; ld65 supplies __LOADER_LOAD__ (somewhere in $20xx-$3xxx) and
        ; __LOADER_SIZE__ (template byte count). The loader was
        ; assembled for run = $0300, so the bytes are position-
        ; dependent — the destination has to be exactly $0300.
        ldx     #0
:       lda     __LOADER_LOAD__,x
        sta     __LOADER_RUN__,x
        inx
        cpx     #<__LOADER_SIZE__
        bne     :-
        ; (Assumes loader size < 256 B. The .cfg LOADER memory area
        ; is 256 B; build fails at link time if the loader exceeds it.)

        ; --- JMP to the loader at $0300 (never returns) ---
        jmp     __LOADER_RUN__

; ============================================================
; SWIFTAUX chain
; ============================================================
;
; Loads SWIFTAUX.SYSTEM (4-byte header [main_size, park_size] + main +
; park; see tools/host/diskimg/pack_swiftaux.py). The main image lands at
; $2000 (cc65 crt0 copies its trailing LC bytes to built-in LC bank 2).
; The park is the XLC copy-down body slots; the aux loader stages it
; into AUX main RAM at AUX_PARK ($2000 aux) via ROM AUXMOVE, so MLI's
; body in built-in LC bank 1 stays intact (same MLI-safety as SWIFTSAT,
; minus the Saturn bank dance). The runtime trampoline (aux_xlc.s) copies
; one slot down to STAGING per builtin call.
.import __LOADER_AUX_LOAD__, __LOADER_AUX_SIZE__, __LOADER_AUX_RUN__

; ------------------------------------------------------------
; void a_install_and_chain_swiftaux(void)
; Caller has done MLI OPEN (g_open_refnum set). Reads the 4-byte header,
; copies LOADER_AUX to $0300, patches the CLOSE refnum, and JMPs the
; loader. Never returns. (No Saturn slot to stash — a //e has none.)
; ------------------------------------------------------------
_a_install_and_chain_swiftaux:
        ; --- MLI READ 4-byte header → SX_HEADER_BUF ---
        lda     _g_open_refnum
        sta     ZP_READ_PARMS+1
        lda     #<SX_HEADER_BUF
        sta     ZP_READ_PARMS+2
        lda     #>SX_HEADER_BUF
        sta     ZP_READ_PARMS+3
        lda     #4                      ; request count: 4-byte header
        sta     ZP_READ_PARMS+4
        lda     #0
        sta     ZP_READ_PARMS+5
        lda     #4                      ; pcount
        sta     ZP_READ_PARMS+0
        lda     LC_BANK1_RO             ; MLI expects bank-1 RAM read
        jsr     MLI
        .byte   MLI_READ
        .word   ZP_READ_PARMS

        ; --- Patch refnum into ZP_CLOSE_PARMS (for the loader's CLOSE) ---
        lda     _g_open_refnum
        sta     ZP_CLOSE_PARMS+1
        lda     #1                      ; pcount
        sta     ZP_CLOSE_PARMS+0

        ; --- Copy LOADER_AUX segment from MAIN-load-addr to $0300 ---
        ldx     #0
:       lda     __LOADER_AUX_LOAD__,x
        sta     __LOADER_AUX_RUN__,x
        inx
        cpx     #<__LOADER_AUX_SIZE__
        bne     :-

        jmp     __LOADER_AUX_RUN__

; ============================================================
.segment "LOADER"

; Assembled for run-address $0300 (per boot_launcher.cfg). Internal JSRs
; and JMPs use absolute addresses that resolve to $0300 + offset.
; load = MAIN means the bytes are stashed in the boot launcher's $2000+
; image until a_install_and_chain_swiftsat memcpys them to $0300.

sx_loader:
        ; ===== Step 0: Patch Saturn switches from SX_SAT_SLOT =====
        ; SX_SAT_SLOT (set by install fn at $1B04) holds the slot
        ; number 0..7. Bank-select switches for Saturn at slot N live
        ; at $C0(N+8)x — low byte = (N+8)*16 + offset, high byte
        ; always $C0.
        ;
        ; Per AppleWin LanguageCard.cpp, the bank-decode formula is
        ; `bank = ((addr>>1)&4) | (addr&3)`. That makes the soft-switch
        ; offsets map to banks 0-indexed:
        ;
        ;   offset 4 → bank 0 (LC-compat: MLI body + cc65 LC live here)
        ;   offset 5 → bank 1 (first extras bank — XLC's home)
        ;   offset 6 → bank 2
        ;   offset 7 → bank 3
        ;   offset C → bank 4 ... offset F → bank 7
        ;
        ; So XLC uses offset 5 (extras bank 1, separate physical 16K
        ; from the LC-compat bank). offset 4 selects bank 0 — used
        ; after memcpy to restore the LC-compat bank so MLI body is
        ; visible for CLOSE and cc65 crt0's LC copy lands there.
        ; offset 3 = "R-RAM bank A, WE" (standard 16K LC convention,
        ; needs 2 reads to arm the write-enable latch).
        lda     SX_SAT_SLOT
        clc
        adc     #8
        asl
        asl
        asl
        asl                              ; A = (slot+8)*16 (slot-base lo)
        pha                              ; stash slot-base for the bank-0 + WE patches
        ora     #$05                     ; bank-1-select offset
        sta     sx_sat_sel_bank1+1
        pla
        pha                              ; restash for the WE patches
        ora     #$04                     ; bank-0-restore offset
        sta     sx_sat_sel_bank0+1
        pla
        ora     #$03                     ; R-RAM-WE offset
        sta     sx_sat_we1+1
        sta     sx_sat_we2+1

        ; ===== Step 1: MLI READ main → $2000 =====
        ; Patch ZP_READ_PARMS: buffer=$2000, count=SX_HEADER_BUF[0..1]
        lda     #<$2000
        sta     ZP_READ_PARMS+2
        lda     #>$2000
        sta     ZP_READ_PARMS+3
        lda     SX_HEADER_BUF+0          ; main_size lo
        sta     ZP_READ_PARMS+4
        lda     SX_HEADER_BUF+1          ; main_size hi
        sta     ZP_READ_PARMS+5
        jsr     MLI
        .byte   MLI_READ
        .word   ZP_READ_PARMS
        ; The main image just landed at $2000+ (includes cc65's LC
        ; bytes appended right after ONCE; cc65's crt0 will memcpy
        ; them to bank 2 at startup). The boot launcher C code that was
        ; at $2000+ is now overwritten — we never return to it.

        ; ===== Step 2: XLC chunked staging =====
        ; Read xlc_size bytes into the $0800 scratch in chunks of up to
        ; SX_XLC_CHUNK ($0400) B, memcpy'ing each chunk to Saturn bank 1
        ; at a running $D000+ destination, until all bytes are staged.
        ; xlc_size is build-time-capped at the $3000 Saturn-bank-1 budget
        ; by pack_swiftsat.py, so no runtime ceiling check is needed.
        ; Empty XLC (xlc_size = 0) skips the whole block.
        ; (replaced the commit-3a single-shot path, which halted at $FAA9
        ; once XLC outgrew the 4864 B scratch.)
        lda     SX_HEADER_BUF+2          ; xlc_size lo
        sta     ZP_SX_REMAIN
        lda     SX_HEADER_BUF+3          ; xlc_size hi
        sta     ZP_SX_REMAIN+1
        ora     ZP_SX_REMAIN
        bne     sx_xlc_stage             ; non-empty → stage it
        jmp     sx_xlc_done              ; empty XLC — skip (too far for beq)
sx_xlc_stage:

        ; Running Saturn-bank-1 destination pointer, starts at $D000.
        lda     #$00
        sta     ZP_SX_DST
        lda     #$D0
        sta     ZP_SX_DST+1

sx_chunk_loop:
        ; chunk = min(REMAIN, SX_XLC_CHUNK). SX_XLC_CHUNK's low byte is
        ; 0, so REMAIN < CHUNK exactly when REMAIN's high byte < CHUNK's.
        lda     ZP_SX_REMAIN+1
        cmp     #>SX_XLC_CHUNK
        bcc     sx_chunk_use_remain
        lda     #<SX_XLC_CHUNK
        sta     ZP_SX_CHUNK
        lda     #>SX_XLC_CHUNK
        sta     ZP_SX_CHUNK+1
        jmp     sx_chunk_read
sx_chunk_use_remain:
        lda     ZP_SX_REMAIN
        sta     ZP_SX_CHUNK
        lda     ZP_SX_REMAIN+1
        sta     ZP_SX_CHUNK+1
sx_chunk_read:
        ; MLI READ chunk → SX_XLC_SCRATCH ($0800). Built-in LC is in
        ; bank-1 R/O (left by the main READ on pass 1, by the per-pass
        ; restore on later passes), so MLI's body is mapped. The shared
        ; refnum advances the file mark, so successive READs walk the
        ; XLC image contiguously.
        lda     #<SX_XLC_SCRATCH
        sta     ZP_READ_PARMS+2
        lda     #>SX_XLC_SCRATCH
        sta     ZP_READ_PARMS+3
        lda     ZP_SX_CHUNK
        sta     ZP_READ_PARMS+4
        lda     ZP_SX_CHUNK+1
        sta     ZP_READ_PARMS+5
        jsr     MLI
        .byte   MLI_READ
        .word   ZP_READ_PARMS

        ; --- Cede the $D000 bus: built-in LC → ROM-read; Saturn bank 1
        ; select + write-enable. docs/contributing/LESSONS.md 2026-05-26: built-in LC must
        ; yield so writes land in Saturn bank 1. ---
        bit     LC_ROM_RO                ; $C082 — built-in to ROM-read, WP
sx_sat_sel_bank1:
        bit     $C0F4                    ; patched: $C0(slot+8)4 = bank 1 select
sx_sat_we1:
        lda     $C0F3                    ; patched: $C0(slot+8)3 (R-RAM,WE prewrite)
sx_sat_we2:
        lda     $C0F3                    ; patched: $C0(slot+8)3 (R-RAM,WE armed)

        ; --- Memcpy scratch → (ZP_SX_DST), ZP_SX_CHUNK bytes. The page
        ; loop advances ZP_SX_DST+1 by chunk_hi; we add chunk_lo after. ---
        lda     #<SX_XLC_SCRATCH
        sta     ZP_SX_SRC
        lda     #>SX_XLC_SCRATCH
        sta     ZP_SX_SRC+1

        ldx     ZP_SX_CHUNK+1            ; full pages first
        beq     sx_copy_partial
sx_copy_page:
        ldy     #0
:       lda     (ZP_SX_SRC),y
        sta     (ZP_SX_DST),y
        iny
        bne     :-
        inc     ZP_SX_SRC+1
        inc     ZP_SX_DST+1
        dex
        bne     sx_copy_page
sx_copy_partial:
        ldy     #0
        lda     ZP_SX_CHUNK              ; partial bytes (lo of count)
        beq     sx_copy_done
:       lda     (ZP_SX_SRC),y
        sta     (ZP_SX_DST),y
        iny
        cpy     ZP_SX_CHUNK
        bne     :-
sx_copy_done:
        ; Advance dst past the partial bytes (the page loop already moved
        ; it past the full pages), so it points just past this chunk.
        clc
        lda     ZP_SX_DST
        adc     ZP_SX_CHUNK
        sta     ZP_SX_DST
        lda     ZP_SX_DST+1
        adc     #0
        sta     ZP_SX_DST+1

        ; --- Restore for the next MLI READ: Saturn → bank 0 (LC-compat,
        ; where MLI body + cc65 LC live), built-in LC → bank-1 R/O so
        ; MLI's body is mapped. On slot 0 these are the same Saturn card;
        ; the sequence leaves it in MLI's expected state. ---
sx_sat_sel_bank0:
        bit     $C0F4                    ; patched: $C0(slot+8)4 = bank 0 select
        bit     LC_BANK1_RO              ; $C088

        ; REMAIN -= chunk; loop until every XLC byte is staged.
        sec
        lda     ZP_SX_REMAIN
        sbc     ZP_SX_CHUNK
        sta     ZP_SX_REMAIN
        lda     ZP_SX_REMAIN+1
        sbc     ZP_SX_CHUNK+1
        sta     ZP_SX_REMAIN+1
        ora     ZP_SX_REMAIN
        beq     sx_xlc_done              ; all staged → exit (short)
        jmp     sx_chunk_loop            ; more → loop (too far for bne)

sx_xlc_done:

        ; ===== Step 3: MLI CLOSE =====
        ; MLI's body in built-in LC bank 1 stayed mapped throughout
        ; (we never switched away from bank 1 R/O for built-in).
        ; CLOSE works normally — no leaked refnum.
        jsr     MLI
        .byte   MLI_CLOSE
        .word   ZP_CLOSE_PARMS

        ; ===== Step 4: Launch SWIFTSAT =====
        ; Hand off to cc65's crt0 in ROM-read state (mimicking
        ; ProDOS's SYS-launch idle). crt0 then does its own bit $C081
        ; / bltu2 / bit $C080 dance to populate built-in LC bank 2
        ; with cc65 LC code.
        bit     LC_ROM_RO                ; $C082 — ROM-read, WP
        jmp     $2000

; ============================================================
.segment "LOADER_AUX"

; Aux park-staging loader. Assembled for run-address
; $0300 (overlays the SWIFTSAT LOADER — the two chain paths are mutually
; exclusive). Stages the park into AUX main RAM via ROM AUXMOVE.
;
; AUXMOVE ($C311) lives in the //e internal $Cxxx ROM, but MLI's
; slot-based disk driver needs slot-card ROM visible — so we toggle
; INTCXROM in ONLY across each AUXMOVE call and restore SLOTCXROM for
; the next MLI READ.

AUXMOVE      = $C311
SETSLOTCXROM = $C006
SETINTCXROM  = $C007
AM_A1L = $3C
AM_A1H = $3D
AM_A2L = $3E
AM_A2H = $3F
AM_A4L = $42
AM_A4H = $43
AUX_PARK_LO = $00                        ; AUX_PARK = $2000 in aux main RAM
AUX_PARK_HI = $20

sx_aux_loader:
        ; ===== Step 1: MLI READ main → $2000 =====
        lda     #<$2000
        sta     ZP_READ_PARMS+2
        lda     #>$2000
        sta     ZP_READ_PARMS+3
        lda     SX_HEADER_BUF+0          ; main_size lo
        sta     ZP_READ_PARMS+4
        lda     SX_HEADER_BUF+1          ; main_size hi
        sta     ZP_READ_PARMS+5
        jsr     MLI
        .byte   MLI_READ
        .word   ZP_READ_PARMS

        ; ===== Step 2: stage park → AUX main RAM (AUXMOVE main→aux) =====
        lda     SX_HEADER_BUF+2          ; park_size lo
        sta     ZP_SX_REMAIN
        lda     SX_HEADER_BUF+3          ; park_size hi
        sta     ZP_SX_REMAIN+1
        ora     ZP_SX_REMAIN
        bne     sxa_stage
        jmp     sxa_done                 ; empty park (too far for beq)
sxa_stage:
        ; Running AUX destination pointer, starts at AUX_PARK.
        lda     #AUX_PARK_LO
        sta     ZP_SX_DST
        lda     #AUX_PARK_HI
        sta     ZP_SX_DST+1

sxa_chunk_loop:
        ; chunk = min(REMAIN, SX_XLC_CHUNK). CHUNK's low byte is 0.
        lda     ZP_SX_REMAIN+1
        cmp     #>SX_XLC_CHUNK
        bcc     sxa_use_remain
        lda     #<SX_XLC_CHUNK
        sta     ZP_SX_CHUNK
        lda     #>SX_XLC_CHUNK
        sta     ZP_SX_CHUNK+1
        jmp     sxa_read
sxa_use_remain:
        lda     ZP_SX_REMAIN
        sta     ZP_SX_CHUNK
        lda     ZP_SX_REMAIN+1
        sta     ZP_SX_CHUNK+1
sxa_read:
        ; MLI READ chunk → $0800 scratch (slot ROM visible; built-in LC
        ; bank-1 RO so MLI body is mapped).
        lda     #<SX_XLC_SCRATCH
        sta     ZP_READ_PARMS+2
        lda     #>SX_XLC_SCRATCH
        sta     ZP_READ_PARMS+3
        lda     ZP_SX_CHUNK
        sta     ZP_READ_PARMS+4
        lda     ZP_SX_CHUNK+1
        sta     ZP_READ_PARMS+5
        jsr     MLI
        .byte   MLI_READ
        .word   ZP_READ_PARMS

        ; --- AUXMOVE main→aux: A1 = $0800, A2 = $0800+chunk-1, A4 = dst ---
        lda     #<SX_XLC_SCRATCH
        sta     AM_A1L
        lda     #>SX_XLC_SCRATCH
        sta     AM_A1H
        ; A2 = scratch + chunk - 1
        clc
        lda     #<SX_XLC_SCRATCH
        adc     ZP_SX_CHUNK
        sta     AM_A2L
        lda     #>SX_XLC_SCRATCH
        adc     ZP_SX_CHUNK+1
        sta     AM_A2H
        lda     AM_A2L
        sec
        sbc     #1
        sta     AM_A2L
        lda     AM_A2H
        sbc     #0
        sta     AM_A2H
        ; A4 = running AUX destination
        lda     ZP_SX_DST
        sta     AM_A4L
        lda     ZP_SX_DST+1
        sta     AM_A4H
        ; Copy, with internal $Cxxx ROM in only across the ROM call.
        sta     SETINTCXROM              ; (A value ignored — soft switch)
        sec                              ; carry set = main → aux
        jsr     AUXMOVE
        sta     SETSLOTCXROM             ; restore slot ROM for next MLI READ

        ; Advance AUX dst += chunk.
        clc
        lda     ZP_SX_DST
        adc     ZP_SX_CHUNK
        sta     ZP_SX_DST
        lda     ZP_SX_DST+1
        adc     ZP_SX_CHUNK+1
        sta     ZP_SX_DST+1

        ; REMAIN -= chunk; loop until the whole park is staged.
        sec
        lda     ZP_SX_REMAIN
        sbc     ZP_SX_CHUNK
        sta     ZP_SX_REMAIN
        lda     ZP_SX_REMAIN+1
        sbc     ZP_SX_CHUNK+1
        sta     ZP_SX_REMAIN+1
        ora     ZP_SX_REMAIN
        beq     sxa_done
        jmp     sxa_chunk_loop

sxa_done:
        ; ===== Step 3: MLI CLOSE =====
        jsr     MLI
        .byte   MLI_CLOSE
        .word   ZP_CLOSE_PARMS

        ; ===== Step 4: Launch SWIFTAUX =====
        bit     LC_ROM_RO                ; ROM-read, WP (cc65 crt0 handoff)
        jmp     $2000

; ============================================================
.segment "DATA"

; MLI OPEN param block. open_parms+1 / +2 (the pathname pointer) is
; patched at runtime from _g_open_pathname. open_parms+5 (refnum out)
; is read after MLI returns success.
open_parms:
        .byte   3               ; pcount
        .word   0               ; pathname pointer (patched)
        .word   IOBUF           ; io_buf
        .byte   0               ; refnum (out)

; ============================================================
; Stage a startup program for the LITE interpreter.
;
; The lite chain's main READ writes $2000+ only, so a program read
; into STAGED_SRC (below $2000) before chaining survives untouched
; into the interpreter's repl_run, which compiles it once at startup.
; (The SWIFTSAT/SWIFTAUX loaders use $0800-$17FF as XLC scratch, so
; they can't stage here pre-chain — they get length 0 for now and a
; later slice stages them after their copy-down completes.)
; ============================================================
.segment "CODE"

; Must match STAGED_SRC_ADDR in src/common/config.h (the address the
; interpreter reads the staged source from).
STAGED_SRC      = $0C00

; uint16_t a_mli_read_startup(void)
; READ the already-open file (g_open_refnum, set by a prior a_mli_open)
; into STAGED_SRC, up to FILE_SRC_SIZE bytes, then CLOSE. Returns the
; transfer count (A = lo, X = hi). A READ result of $4C (END OF DATA) is
; the normal whole-file case and is ignored; the transfer count is valid
; regardless.
_a_mli_read_startup:
        lda     _g_open_refnum
        sta     su_read_parms+1
        sta     su_close_parms+1
        jsr     MLI
        .byte   MLI_READ
        .word   su_read_parms
        jsr     MLI
        .byte   MLI_CLOSE
        .word   su_close_parms
        lda     su_read_parms+6     ; transfer count lo
        ldx     su_read_parms+7     ; transfer count hi
        rts

.segment "DATA"

su_read_parms:
        .byte   4               ; pcount
        .byte   0               ; refnum (patched)
        .word   STAGED_SRC      ; data buffer
        .word   2048            ; request count (FILE_SRC_SIZE)
        .word   0               ; transfer count (out)
su_close_parms:
        .byte   1               ; pcount
        .byte   0               ; refnum (patched)

; ============================================================
; File-manager mini-UI MLI verbs
; ============================================================
; Each returns the MLI error code (0 = success) in A — the standard
; MLI convention (BCS/Z reflects it; we just leave A as fastcall
; return). The directory READ returns the 16-bit transfer count in
; A (lo) / X (hi). All param blocks live in DATA below; the path
; buffers (_g_prefix / _g_path / _g_path2) live in C BSS.

.segment "CODE"

; uint8_t a_mli_get_prefix(void) — GET_PREFIX into _g_prefix (len-prefixed).
_a_mli_get_prefix:
        jsr     MLI
        .byte   MLI_GET_PREFIX
        .word   gp_parms
        rts

; uint8_t a_mli_set_prefix(void) — SET_PREFIX to _g_path (len-prefixed).
_a_mli_set_prefix:
        jsr     MLI
        .byte   MLI_SET_PREFIX
        .word   sp_parms
        rts

; uint16_t a_mli_read_dirblk(void) — READ up to 512 B of the open
; directory (refnum in _g_open_refnum) into DIRBLK. Returns xfer count.
_a_mli_read_dirblk:
        lda     _g_open_refnum
        sta     dr_parms+1
        jsr     MLI
        .byte   MLI_READ
        .word   dr_parms
        lda     dr_parms+6      ; transfer count lo
        ldx     dr_parms+7      ; transfer count hi
        rts

; uint8_t a_mli_close(void) — CLOSE the open refnum.
_a_mli_close:
        lda     _g_open_refnum
        sta     cl_parms+1
        jsr     MLI
        .byte   MLI_CLOSE
        .word   cl_parms
        rts

; uint8_t a_mli_destroy(void) — DESTROY _g_path (relative to prefix).
_a_mli_destroy:
        jsr     MLI
        .byte   MLI_DESTROY
        .word   ds_parms
        rts

; uint8_t a_mli_rename(void) — RENAME _g_path -> _g_path2.
_a_mli_rename:
        jsr     MLI
        .byte   MLI_RENAME
        .word   rn_parms
        rts

; uint8_t a_mli_create_dir(void) — CREATE _g_path as a subdirectory
; (storage type $0D, file type $0F = DIR).
_a_mli_create_dir:
        jsr     MLI
        .byte   MLI_CREATE
        .word   cr_parms
        rts

; uint8_t a_mli_online(void) — ON_LINE all units into _g_online (256 B). Each
; 16-byte record: byte 0 = (unit_num << 4) | name_length; bytes 1.. = volume
; name (no leading '/'). A zero name_length means that slot has no mounted
; volume (byte 1 is the MLI error for it) — the C side skips those.
_a_mli_online:
        jsr     MLI
        .byte   MLI_ONLINE
        .word   on_parms
        rts

; ------------------------------------------------------------
; Resume note ("LASTRUN") verbs. The note records the
; directory + filename of the last program run from the browser so a
; cold reboot (via the interpreter's :quit) can re-open the browser on
; it. All operate on the absolute path in _g_notepath; OPEN reuses
; a_mli_open (g_open_pathname = _g_notepath) and shares open_parms/IOBUF.
; ------------------------------------------------------------

; uint8_t a_mli_destroy_note(void) — DESTROY _g_notepath (ignore errors).
_a_mli_destroy_note:
        jsr     MLI
        .byte   MLI_DESTROY
        .word   dn_parms
        rts

; uint8_t a_mli_create_note(void) — CREATE _g_notepath (TXT, seedling).
_a_mli_create_note:
        jsr     MLI
        .byte   MLI_CREATE
        .word   cn_parms
        rts

; void a_mli_write_note(void) — WRITE _g_note_len bytes from _g_note to
; the open note (refnum in _g_open_refnum).
_a_mli_write_note:
        lda     _g_open_refnum
        sta     wn_parms+1
        lda     _g_note_len
        sta     wn_parms+4
        lda     _g_note_len+1
        sta     wn_parms+5
        jsr     MLI
        .byte   MLI_WRITE
        .word   wn_parms
        rts

; uint16_t a_mli_read_note(void) — READ up to 96 B of the open note into
; _g_note. Returns the transfer count (A = lo, X = hi).
_a_mli_read_note:
        lda     _g_open_refnum
        sta     rnote_parms+1
        jsr     MLI
        .byte   MLI_READ
        .word   rnote_parms
        lda     rnote_parms+6
        ldx     rnote_parms+7
        rts

.ifdef LITE_IIE
; uint8_t a_mli_read_block(void) — READ_BLOCK _g_rb_block from device
; _g_rb_unit into DIRBLK ($0800). Returns the MLI error (0 = ok). Used by
; the volume picker to read a volume's directory header (block 2) and its
; allocation bitmap. Block reads are device-level, so they work regardless
; of the current prefix / open refnum. //e launcher only (the disk-space
; readout ships on the //e disk; the II+ launcher drops it for BSS budget).
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
.endif

.segment "DATA"

; GET_PREFIX: count=1, data buffer (>= 64 B, len-prefixed result).
gp_parms:
        .byte   1
        .word   _g_prefix

; ON_LINE: count=2, unit_num=0 (all online volumes), 256-byte result buffer.
on_parms:
        .byte   2
        .byte   0
        .word   _g_online

; SET_PREFIX: count=1, pathname (len-prefixed).
sp_parms:
        .byte   1
        .word   _g_path

; READ (directory block): count=4, refnum, buffer, request, xfer-out.
dr_parms:
        .byte   4               ; pcount
        .byte   0               ; refnum (patched)
        .word   DIRBLK          ; data buffer ($0800)
        .word   512             ; request count (one ProDOS block)
        .word   0               ; transfer count (out)

.ifdef LITE_IIE
; READ_BLOCK: count=3, unit_num (patched), data buffer (DIRBLK), block (patched).
rb_parms:
        .byte   3
        .byte   0               ; unit_num (patched from _g_rb_unit)
        .word   DIRBLK          ; data buffer ($0800)
        .word   0               ; block_num (patched from _g_rb_block)
.endif

; CLOSE: count=1, refnum.
cl_parms:
        .byte   1               ; pcount
        .byte   0               ; refnum (patched)

; DESTROY: count=1, pathname.
ds_parms:
        .byte   1
        .word   _g_path

; RENAME: count=2, pathname, new pathname.
rn_parms:
        .byte   2
        .word   _g_path
        .word   _g_path2

; CREATE: count=7. access $C3 (full), file_type $0F (DIR),
; aux_type 0, storage_type $0D (subdirectory), create date/time 0
; (ProDOS fills the current date/time when these are 0).
cr_parms:
        .byte   7
        .word   _g_path         ; pathname
        .byte   $C3             ; access
        .byte   $0F             ; file_type = DIR
        .word   0               ; aux_type
        .byte   $0D             ; storage_type = subdirectory
        .word   0               ; create_date
        .word   0               ; create_time

; DESTROY note: count=1, the absolute LASTRUN path.
dn_parms:
        .byte   1
        .word   _g_notepath

; CREATE note: count=7. access $C3 (full), file_type $04 (TXT),
; aux_type 0, storage_type $01 (seedling), create date/time 0.
cn_parms:
        .byte   7
        .word   _g_notepath     ; pathname
        .byte   $C3             ; access
        .byte   $04             ; file_type = TXT
        .word   0               ; aux_type
        .byte   $01             ; storage_type = seedling
        .word   0               ; create_date
        .word   0               ; create_time

; WRITE note: count=4, refnum (patched), buffer, request (patched from
; _g_note_len), transfer count (out).
wn_parms:
        .byte   4
        .byte   0               ; refnum (patched)
        .word   _g_note         ; data buffer
        .word   0               ; request count (patched)
        .word   0               ; transfer count (out)

; READ note: count=4, refnum (patched), buffer, request=96, xfer-out.
rnote_parms:
        .byte   4
        .byte   0               ; refnum (patched)
        .word   _g_note         ; data buffer
        .word   96              ; request count
        .word   0               ; transfer count (out)

