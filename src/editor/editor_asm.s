; editor_asm.s — small ProDOS MLI helper for the in-launcher editor.
;
; The editor's ordinary file I/O goes through prodos.c's raw-MLI wrapper
; (fixed $1C00 buffer). This standalone helper supplies GET_PREFIX so the
; editor can absolutise a bare save-as / open name against the boot volume.
; cc65's inline __asm__ can't emit the `.byte cmd / .word params` argument
; trailer an MLI call requires, so that one call lives here, mirroring the
; launcher's a_mli_get_prefix (boot_launcher_asm.s).

        .export _editor_get_prefix

MLI            = $BF00
MLI_GET_PREFIX = $C7

.segment "CODE"

; void __fastcall__ editor_get_prefix(unsigned char *buf);
;   buf (A = lo, X = hi) receives the current ProDOS prefix length-prefixed:
;   [len][chars including the trailing '/'].  buf must be >= 65 bytes.
;   On a no-prefix system MLI returns len 0.  The C side checks buf[0].
_editor_get_prefix:
        sta     gp_parm + 1     ; data-buffer pointer lo
        stx     gp_parm + 2     ; data-buffer pointer hi
        jsr     MLI
        .byte   MLI_GET_PREFIX
        .word   gp_parm
        rts

.segment "DATA"
gp_parm:
        .byte   1               ; param count
        .word   0               ; data buffer pointer (set per call)
