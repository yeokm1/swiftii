; src/platform/apple2/saturn_bc.s — Family B II+ Saturn bytecode store driver.
;
; The low-level Saturn-bank driver behind the shared bytecode
; store (src/common/aux_store.c, -DBC_STORE_SATURN): the II+ Runner pages
; bytecode IN from Saturn banks (bcwin.c) and the II+ Compiler flushes bytecode
; OUT to them (bcbuf.c). It is the Saturn counterpart of aux_bc.s — same store
; interface (the _bc_aux_* param globals + page/stage workers), different RAM.
;
; The Compiler and Runner are MAIN-only binaries (swiftii-{compiler,runner}.cfg:
; all cc65 code lives in $2000-$BEFF; the language card holds ONLY ProDOS's MLI
; body). So unlike SWIFTAUX's aux trampoline, this driver never hides executing
; code when it banks the $D000-$FFFF LC region over to a Saturn bank — the copy
; loop and its MAIN buffer stay visible throughout. We only have to restore MLI's
; expected LC state ($C088, bank-1 R/O) before returning, so the next file I/O
; works. The bank dance (cede the built-in LC, select a Saturn bank, write-enable,
; copy, restore) mirrors the proven LOADER stager in boot_launcher_asm.s, which
; runs on real II+ Saturn hardware.
;
; Park layout: bytecode lives in Saturn banks 1..7 (bank 0 is reserved for the
; MLI body + cc65 LC). Each bank exposes 12 KB at $D000-$FFFF (bank-A / "LC
; bank 2" convention, the same window the LOADER stages XLC into), so image
; offset 0 = bank 1 $D000. 7 banks * 12 KB = 84 KB of capacity (the 16-bit
; image offset caps the practical max well under that).
;
; Slot handling: a Saturn at slot N (N != 0) coexists with the machine's own
; 16 KB built-in LC; reads/writes must take the built-in OFF the $D000 bus
; (bit $C082, ROM-read) so Saturn drives it. A Saturn AT slot 0 IS the language
; card — there is no second LC to fight, so the cede becomes a harmless
; read-mode set (bit $C080). saturn_bc_init() patches the cede + the slot-based
; bank-select/write-enable switches once at main() entry from the slot the boot
; launcher parked at SX_SAT_SLOT ($1B04). This is the same slot-conditioning
; xlc.s documents at length.
;
; Parameters (the xlc_argc convention; aux_store.c sets these, then calls a
; no-arg worker):
;   _bc_aux_off : image offset into the park (bank 0 of the park = bank 1 $D000)
;   _bc_aux_ptr : MAIN buffer address (dest for page, source for stage)
;   _bc_aux_n   : byte count
;
;   void store_page(void)    store -> main : read into the MAIN window
;   void store_stage(void)   main -> store : flush from the MAIN window
;   void __fastcall__ saturn_bc_init(uint8_t slot)  : patch switches for `slot`
;
; KEEP IN SYNC: BANK_BYTES / PARK_HI / DATA_BANK_FIRST here and bcwin.c's
; image-offset model; the slot-base math here and xlc.s / boot_launcher_asm.s.

; --- monitor zero-page scratch (free during this driver: it calls no MLI/cc65
; LC code, and these are recomputed every call, so nothing persists across the
; MLI file I/O that uses the same A1-A4 cells). ---
SRC      = $3C                  ; .. $3D copy source pointer
DST      = $3E                  ; .. $3F copy dest pointer
SB_WITHIN = $40                 ; .. $41 byte offset within the current bank
SB_REM    = $42                 ; .. $43 bytes left to copy
SB_CHUNK  = $44                 ; .. $45 bytes to copy this bank

LC_BANK1_RO = $C088             ; built-in (or Saturn slot-0) LC: R-RAM bank 1,
                                ; write-protect — MLI's expected state on return

BANK_BYTES     = $3000          ; usable bytes per Saturn bank ($D000-$FFFF)
PARK_HI        = $D0            ; bank window base $D000 (low byte 0)
DATA_BANK_FIRST = 1             ; image offset 0 lands in Saturn bank 1

.export _store_page
.export _store_stage
.export _saturn_bc_init
.import _bc_aux_off             ; uint16_t  (aux_store.c)
.import _bc_aux_ptr             ; unsigned char *
.import _bc_aux_n               ; uint16_t

.segment "BSS"
sb_bank:   .res 1               ; current Saturn bank (1..7)
sb_base:   .res 1               ; slot-base low byte = (slot+8)*16
sb_isread: .res 1               ; 1 = page (read), 0 = stage (write)
sb_satlo:  .res 1               ; current Saturn $D000+within pointer
sb_sathi:  .res 1

.segment "CODE"

; ------------------------------------------------------------
; void __fastcall__ saturn_bc_init(uint8_t slot)
; cc65 fastcall: A = slot on entry. Patches the cede + slot-based switches.
; slot = $FF (no Saturn) is a no-op — Tier-2 binaries are only chained on a
; Saturn machine, but guard anyway. Clobbers A, X, Y.
; ------------------------------------------------------------
_saturn_bc_init:
        cmp     #$FF
        bne     :+
        rts                              ; no Saturn — leave switches as template
:
        tax                              ; X = slot (for the slot-0 cede test)

        ; slot-base low byte = (slot+8)*16 = $80 + slot*16 (the standard
        ; "slot N I/O" low byte; for slot 0 this is $80).
        clc
        adc     #8
        asl
        asl
        asl
        asl
        sta     sb_base                  ; base_lo

        ; sat_read = $C0(base+0) : Saturn R-RAM bank A, WP (read enable)
        sta     sat_read+1
        ; sat_we1/2 = $C0(base+3) : Saturn R-RAM bank A, WE (two reads arm write)
        clc
        adc     #3
        sta     sat_we1+1
        sta     sat_we2+1
        ; sat_sel0 = $C0(base+4) : select Saturn bank 0 (restore LC-compat)
        lda     sb_base
        clc
        adc     #4
        sta     sat_sel0+1

        ; sat_cede : built-in $C082 (ROM-read) for slot N; $C080 for slot 0
        ; (Saturn IS the LC — keep it R-RAM read; there's no second LC to cede).
        txa
        beq     cede_slot0
        lda     #$82
        sta     sat_cede+1
        rts
cede_slot0:
        lda     #$80
        sta     sat_cede+1
        rts

; ------------------------------------------------------------
; void store_page(void)  — store -> main (read a window).
; ------------------------------------------------------------
_store_page:
        lda     #1
        sta     sb_isread
        jmp     sb_common

; ------------------------------------------------------------
; void store_stage(void) — main -> store (flush a window).
; ------------------------------------------------------------
_store_stage:
        lda     #0
        sta     sb_isread
        ; fall through

; ------------------------------------------------------------
; Shared body: walk the byte range [_bc_aux_off, +_bc_aux_n) across Saturn
; banks, copying each bank-bounded chunk to/from the MAIN buffer.
; ------------------------------------------------------------
sb_common:
sat_cede:
        bit     $C082                    ; patched: cede built-in ($C082) / slot0 ($C080)

        ; within = off ; bank = 1 ; normalize (off may span banks)
        lda     _bc_aux_off
        sta     SB_WITHIN
        lda     _bc_aux_off+1
        sta     SB_WITHIN+1
        lda     #DATA_BANK_FIRST
        sta     sb_bank
sb_norm:
        lda     SB_WITHIN+1
        cmp     #>BANK_BYTES             ; within_hi < $30 -> within < $3000
        bcc     sb_norm_done
        sec
        lda     SB_WITHIN+1
        sbc     #>BANK_BYTES             ; within -= $3000 (low byte of $3000 = 0)
        sta     SB_WITHIN+1
        inc     sb_bank
        jmp     sb_norm
sb_norm_done:
        ; rem = n
        lda     _bc_aux_n
        sta     SB_REM
        lda     _bc_aux_n+1
        sta     SB_REM+1

sb_loop:
        lda     SB_REM
        ora     SB_REM+1
        bne     sb_have
        jmp     sb_done
sb_have:
        ; chunk = $3000 - within (space left in this bank)
        sec
        lda     #<BANK_BYTES             ; $00
        sbc     SB_WITHIN
        sta     SB_CHUNK
        lda     #>BANK_BYTES             ; $30
        sbc     SB_WITHIN+1
        sta     SB_CHUNK+1
        ; if rem < chunk -> chunk = rem
        lda     SB_REM+1
        cmp     SB_CHUNK+1
        bcc     sb_use_rem
        bne     sb_keep
        lda     SB_REM
        cmp     SB_CHUNK
        bcc     sb_use_rem
        jmp     sb_keep
sb_use_rem:
        lda     SB_REM
        sta     SB_CHUNK
        lda     SB_REM+1
        sta     SB_CHUNK+1
sb_keep:
        ; --- select Saturn bank sb_bank ---
        ; offset(bank) = (bank < 4) ? bank+4 : bank+8
        lda     sb_bank
        cmp     #4
        bcc     sb_lowbank
        clc
        adc     #8
        jmp     sb_offdone
sb_lowbank:
        clc
        adc     #4
sb_offdone:
        clc
        adc     sb_base
        sta     sat_sel+1
sat_sel:
        bit     $C085                    ; patched per chunk: select 16K bank

        ; --- mode: read enable or write enable ---
        lda     sb_isread
        beq     sb_wmode
sat_read:
        bit     $C080                    ; patched: R-RAM bank A, WP (read enable)
        jmp     sb_modedone
sb_wmode:
sat_we1:
        lda     $C083                    ; patched: R-RAM bank A, WE (prewrite 1)
sat_we2:
        lda     $C083                    ; patched: R-RAM bank A, WE (armed)
sb_modedone:
        ; --- Saturn pointer = $D000 + within ---
        lda     SB_WITHIN
        sta     sb_satlo
        lda     SB_WITHIN+1
        clc
        adc     #PARK_HI                 ; within_hi <= $2F so + $D0 never carries out
        sta     sb_sathi
        ; --- set SRC/DST by direction ---
        lda     sb_isread
        beq     sb_dir_write
        ; read: SRC = Saturn, DST = MAIN
        lda     sb_satlo
        sta     SRC
        lda     sb_sathi
        sta     SRC+1
        lda     _bc_aux_ptr
        sta     DST
        lda     _bc_aux_ptr+1
        sta     DST+1
        jmp     sb_copy
sb_dir_write:
        ; write: SRC = MAIN, DST = Saturn
        lda     _bc_aux_ptr
        sta     SRC
        lda     _bc_aux_ptr+1
        sta     SRC+1
        lda     sb_satlo
        sta     DST
        lda     sb_sathi
        sta     DST+1
sb_copy:
        jsr     sb_memcpy                ; copy SB_CHUNK bytes (SRC)->(DST)

        ; --- advance to the next bank ---
        clc
        lda     _bc_aux_ptr              ; ptr += chunk
        adc     SB_CHUNK
        sta     _bc_aux_ptr
        lda     _bc_aux_ptr+1
        adc     SB_CHUNK+1
        sta     _bc_aux_ptr+1
        sec
        lda     SB_REM                   ; rem -= chunk
        sbc     SB_CHUNK
        sta     SB_REM
        lda     SB_REM+1
        sbc     SB_CHUNK+1
        sta     SB_REM+1
        lda     #0                       ; next bank starts at within 0
        sta     SB_WITHIN
        sta     SB_WITHIN+1
        inc     sb_bank
        jmp     sb_loop

sb_done:
sat_sel0:
        bit     $C0F4                    ; patched: $C0(base+4) — Saturn bank 0
        bit     LC_BANK1_RO              ; $C088 — built-in LC bank-1 R/O (MLI)
        rts

; ------------------------------------------------------------
; Copy SB_CHUNK (16-bit) bytes from (SRC) to (DST). Pages first, then the
; partial tail. SRC/DST and SB_CHUNK are clobbered (caller no longer needs them).
; ------------------------------------------------------------
sb_memcpy:
        ldx     SB_CHUNK+1               ; full pages
        beq     sb_mc_part
sb_mc_page:
        ldy     #0
:       lda     (SRC),y
        sta     (DST),y
        iny
        bne     :-
        inc     SRC+1
        inc     DST+1
        dex
        bne     sb_mc_page
sb_mc_part:
        lda     SB_CHUNK                 ; partial bytes (low count)
        beq     sb_mc_done
        ldy     #0
:       lda     (SRC),y
        sta     (DST),y
        iny
        cpy     SB_CHUNK
        bne     :-
sb_mc_done:
        rts
