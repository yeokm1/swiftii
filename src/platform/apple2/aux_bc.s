; src/platform/apple2/aux_bc.s — Family B //e aux bytecode store driver.
;
; The low-level ROM-AUXMOVE driver behind the shared aux store
; (src/common/aux_store.c): the //e Runner pages bytecode IN from aux
; (bcwin.c, -DWITH_AUX_BC) and the //e Compiler flushes bytecode OUT to aux
; (bcbuf.c, -DWITH_AUX_COMPILE). Both tools are MAIN-only and use no aux for
; themselves, so on a //e the auxiliary 64K is free to park program bytecode,
; lifting the program-size ceiling. Params arrive via the _bc_aux_* globals
; (aux_store.c).
;
; This is the same ROM AUXMOVE machinery SWIFTAUX uses (aux_xlc.s), but for
; DATA, not code: we never execute the copied bytes, the VM just reads them
; from the MAIN window. AUXMOVE ($C311) lives in ROM (unaffected by RAMRD/
; RAMWRT) and toggles the aux switches around each byte internally, so a
; hand-rolled aux copy loop (whose own fetches RAMRD would redirect) is
; avoided. We toggle INTCXROM in only across the ROM call so ProDOS MLI's
; slot-card $Cxxx ROM stays visible for the Runner's file I/O everywhere else.
;
; Park layout: bytecode at aux $2000 (AUX_BC_PARK), image offset 0 = $2000.
; That window ($2000..$BFFF aux ≈ 40K) bounds the largest program.
;
; Parameters are passed via the C globals below (the xlc_argc convention),
; then a no-arg worker is called:
;   bc_aux_off : image offset into the park (added to $2000)
;   bc_aux_ptr : MAIN buffer address (dest for page, source for stage)
;   bc_aux_n   : byte count
;
;   void aux_bc_page(void)   aux -> main : read a window (carry clear)
;   void aux_bc_stage(void)  main -> aux : stage at load (carry set)
;
; KEEP IN SYNC: AUX_BC_PARK_HI here and bcwin.c's image-offset model.

; AUXMOVE ($C311) monitor zero-page pointers (Apple //e ROM ref).
A1L     = $3C
A1H     = $3D
A2L     = $3E
A2H     = $3F
A4L     = $42
A4H     = $43
AUXMOVE = $C311
SETSLOTCXROM = $C006                     ; write → slot-card $Cxxx ROM (MLI)
SETINTCXROM  = $C007                     ; write → internal $Cxxx ROM (AUXMOVE)

AUX_BC_PARK_HI = $20                     ; AUX_BC_PARK = $2000 (aux main RAM)

.export _aux_bc_page
.export _aux_bc_stage
.import _bc_aux_off                      ; uint16_t  (bcwin.c)
.import _bc_aux_ptr                      ; unsigned char *
.import _bc_aux_n                        ; uint16_t

.segment "CODE"

; ------------------------------------------------------------
; void aux_bc_page(void)   — copy bc_aux_n bytes from aux park
; (PARK + bc_aux_off) down to MAIN bc_aux_ptr. AUXMOVE aux->main = carry clear.
; ------------------------------------------------------------
_aux_bc_page:
        ; A1 = PARK + off (aux source)
        lda     _bc_aux_off
        sta     A1L
        lda     _bc_aux_off+1
        clc
        adc     #AUX_BC_PARK_HI
        sta     A1H
        jsr     set_a2_end               ; A2 = A1 + n - 1
        ; A4 = bc_aux_ptr (main dest)
        lda     _bc_aux_ptr
        sta     A4L
        lda     _bc_aux_ptr+1
        sta     A4H
        sta     SETINTCXROM
        clc                              ; aux -> main
        jsr     AUXMOVE
        sta     SETSLOTCXROM
        rts

; ------------------------------------------------------------
; void aux_bc_stage(void)  — copy bc_aux_n bytes from MAIN bc_aux_ptr up to
; the aux park (PARK + bc_aux_off). AUXMOVE main->aux = carry set.
; ------------------------------------------------------------
_aux_bc_stage:
        ; A1 = bc_aux_ptr (main source)
        lda     _bc_aux_ptr
        sta     A1L
        lda     _bc_aux_ptr+1
        sta     A1H
        jsr     set_a2_end               ; A2 = A1 + n - 1
        ; A4 = PARK + off (aux dest)
        lda     _bc_aux_off
        sta     A4L
        lda     _bc_aux_off+1
        clc
        adc     #AUX_BC_PARK_HI
        sta     A4H
        sta     SETINTCXROM
        sec                              ; main -> aux
        jsr     AUXMOVE
        sta     SETSLOTCXROM
        rts

; A2 = A1 + bc_aux_n - 1 (inclusive source end). A1 already set by caller.
set_a2_end:
        lda     A1L
        clc
        adc     _bc_aux_n
        sta     A2L
        lda     A1H
        adc     _bc_aux_n+1
        sta     A2H
        lda     A2L                      ; -1 (n is a count; end is inclusive)
        sec
        sbc     #1
        sta     A2L
        lda     A2H
        sbc     #0
        sta     A2H
        rts
