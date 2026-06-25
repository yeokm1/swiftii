; src/platform/apple2/mli.s — minimal ProDOS MLI trampoline.
;
; The Family B Compiler/Runner are MAIN-only (LC empty, ProDOS MLI body
; intact) so they can do real file I/O. cc65's POSIX layer (open/read/write)
; would work, but it costs ~4 KB of code and malloc's a 1 KB ProDOS I/O
; buffer at runtime — which collapses the Compiler's MAIN window and caps
; source size. This wrapper calls MLI directly instead, so prodos.c can do
; file I/O with a fixed low-RAM buffer and no cc65 stdio (the launcher uses
; the same trick — see tools/apple2/boot_launcher/boot_launcher_asm.s).
;
; ProDOS MLI's calling convention puts the command byte and a pointer to the
; parameter block INLINE after `JSR $BF00`; MLI reads them via the return
; address and resumes past them. We self-modify those three bytes per call
; (single-threaded, RAM code), then return MLI's status:
;
;   uint8_t __fastcall__ mli(void *params, uint8_t cmd);
;     cmd    — MLI command byte (rightmost arg -> A)
;     params — pointer to the command's parameter block (pushed -> popax)
;     returns 0 on success, else the MLI error code (carry was set).

.export _mli
.import popax

MLI = $BF00

.segment "CODE"

_mli:
        sta     mli_cmd         ; A = cmd (the rightmost __fastcall__ arg)
        jsr     popax           ; A/X = params pointer (low/high)
        sta     mli_param
        stx     mli_param+1
        jsr     MLI
mli_cmd:    .byte 0             ; \ self-modified inline parameter list that
mli_param:  .word 0             ; / MLI reads via the return address
        bcs     @err            ; carry set -> A holds the error code
        lda     #0
@err:   rts
