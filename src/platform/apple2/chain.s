; src/platform/apple2/chain.s — chain another ProDOS SYS file over $2000.
;
; After the Compiler writes SWIFTII.SWB it chains the Runner, and
; the Runner is a fresh $2000 program. Chaining from a running $2000 binary
; needs a loader that lives OUTSIDE $2000 (the MLI READ overwrites it), so we
; copy a tiny bouncer into the zero page and jump to it — exactly the trick
; the boot launcher uses (tools/apple2/boot_launcher/boot_launcher_asm.s
; _a_install_and_chain). The caller has already MLI-OPENed the target SYS
; file (prodos.c pf_open_read) and passes its ref_num.
;
;   void __fastcall__ chain_exec(uint8_t refnum);
;
; The bouncer (in ZP) does: MLI READ the file to $2000, MLI CLOSE, JMP $2000.
; Never returns. ZP $B0-$C9 is free here (cc65 uses $80-$99, the VM ZP is
; $D0-$DF) and is the same window the launcher uses.

.export _chain_exec

MLI          = $BF00
MLI_READ     = $CA
MLI_CLOSE    = $CC
LC_BANK1_RO  = $C088            ; read RAM bank 1, write-protect (MLI idle state)

ZP_BOUNCER   = $B0              ; .. $BE
ZP_READ_PARMS = $C0            ; .. $C7
ZP_CLOSE_PARMS = $C8           ; .. $C9

.segment "CODE"

_chain_exec:
        pha                     ; save refnum (copy loops clobber A)
        ldx     #(btpl_end - btpl - 1)
:       lda     btpl,x
        sta     ZP_BOUNCER,x
        dex
        bpl     :-
        ldx     #(rtpl_end - rtpl - 1)
:       lda     rtpl,x
        sta     ZP_READ_PARMS,x
        dex
        bpl     :-
        ldx     #(ctpl_end - ctpl - 1)
:       lda     ctpl,x
        sta     ZP_CLOSE_PARMS,x
        dex
        bpl     :-
        pla                     ; refnum
        sta     ZP_READ_PARMS+1
        sta     ZP_CLOSE_PARMS+1
        lda     LC_BANK1_RO     ; MLI expects bank-1 RAM read on entry
        jmp     ZP_BOUNCER

.segment "RODATA"

; Bouncer template copied to ZP_BOUNCER. Its inline .word operands point at
; the ZP-resident param copies because by the time it runs MLI READ has
; wiped the RODATA originals at $2000+.
btpl:
        jsr     MLI
        .byte   MLI_READ
        .word   ZP_READ_PARMS
        jsr     MLI
        .byte   MLI_CLOSE
        .word   ZP_CLOSE_PARMS
        jmp     $2000
btpl_end:

rtpl:                            ; READ param block (refnum patched in)
        .byte   4               ; param count
        .byte   0               ; ref_num (patched)
        .word   $2000           ; data buffer
        .word   $9F00           ; request count (full SYS ceiling; EOF stops short)
        .word   0               ; transfer count (out)
rtpl_end:

ctpl:                            ; CLOSE param block (refnum patched in)
        .byte   1
        .byte   0               ; ref_num (patched)
ctpl_end:
