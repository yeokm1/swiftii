;
; Oliver Schmidt, 2009-09-15
;
; Startup code for cc65 (Apple2 version)
;
; SwiftII fork (2026-06-04): byte-identical to cc65 2.19's libsrc/apple2/crt0.s
; EXCEPT the language-card-segment copy no longer calls the Applesoft ROM. Stock
; cc65 crt0 copies the LC segment to the language card at startup with
; `jsr $D39A` (Applesoft BLTU2), which only exists on the Apple ][+ and later;
; on the original Integer-BASIC Apple ][ that address is unrelated code, so the
; call derails the program right after ProDOS hands off (izapple2 panics; a real
; ][ / Mariani fall into Integer BASIC -> *** RANGE ERR). cc65 2.19 ships no
; apple2-integer-basic-compat.o and hardcodes $D39A, so we substitute the C
; library's own `_memcpy` (already linked) for the copy:
;
;   memcpy(__LC_START__, __ONCE_LOAD__ + __ONCE_SIZE__, __LC_LAST__ - __LC_START__)
;        =  dest (LC run addr)   src (LC load image in MAIN)   n (LC size)
;
; _memcpy is __fastcall__ (dest + src pushed, n in A/X), no-ops a zero-length LC
; (the boot launcher's LC is empty; the interpreters' is ~$2800), and is page-
; optimised — so this is actually SMALLER and FASTER than stock's BLTU2 path and
; needs no guard. The LC load image (in MAIN, < $C000) and its $D000+ run area
; never overlap, so memcpy's low->high copy is safe. Result: every SwiftII cc65
; binary boots on all Apple II generations, the original Integer-BASIC ][
; included, at no size cost (it is a few bytes smaller than stock).
;
; Providing this object on the cl65 input line defines __STARTUP__/_exit/done/
; return, so ld65 never pulls crt0.o from apple2.lib. Re-sync with upstream if
; the toolchain's crt0 changes (see docs/contributing/LESSONS.md).
;

        .export         _exit, done, return
        .export         __STARTUP__ : absolute = 1      ; Mark as startup

        .import         initlib, donelib
        .import         zerobss, callmain
        .import         pushax, _memcpy                 ; ROM-free LC copy
        .import         __ONCE_LOAD__, __ONCE_SIZE__    ; Linker generated
        .import         __LC_START__, __LC_LAST__       ; Linker generated

        .include        "zeropage.inc"
        .include        "apple2.inc"

; ------------------------------------------------------------------------

        .segment        "STARTUP"

        ; ProDOS TechRefMan, chapter 5.2.1:
        ; "For maximum interrupt efficiency, a system program should not
        ;  use more than the upper 3/4 of the stack."
        ldx     #$FF
        txs                     ; Init stack pointer

        ; Save space by putting some of the start-up code in the ONCE segment,
        ; which can be re-used by the BSS segment, the heap and the C stack.
        jsr     init

        ; Clear the BSS data.
        jsr     zerobss

        ; Push the command-line arguments; and, call main().
        jsr     callmain

        ; Avoid a re-entrance of donelib. This is also the exit() entry.
_exit:  ldx     #<exit
        lda     #>exit
        jsr     reset           ; Setup RESET vector

        ; Switch in ROM, in case it wasn't already switched in by a RESET.
        bit     $C082

        ; Call the module destructors.
        jsr     donelib

        ; Restore the original RESET vector.
exit:   ldx     #$02
:       lda     rvsave,x
        sta     SOFTEV,x
        dex
        bpl     :-

        ; Copy back the zero-page stuff.
        ldx     #zpspace-1
:       lda     zpsave,x
        sta     sp,x
        dex
        bpl     :-

        ; ProDOS TechRefMan, chapter 5.2.1:
        ; "System programs should set the stack pointer to $FF at the
        ;  warm-start entry point."
        ldx     #$FF
        txs                     ; Re-init stack pointer

        ; We're done
        jmp     done

; ------------------------------------------------------------------------

        .segment        "ONCE"

        ; Save the zero-page locations that we need.
init:   ldx     #zpspace-1
:       lda     sp,x
        sta     zpsave,x
        dex
        bpl     :-

        ; Save the original RESET vector.
        ldx     #$02
:       lda     SOFTEV,x
        sta     rvsave,x
        dex
        bpl     :-

        ; Check for ProDOS.
        ldy     $BF00           ; MLI call entry point
        cpy     #$4C            ; Is MLI present? (JMP opcode)
        bne     basic

        ; Check the ProDOS system bit map.
        lda     $BF6F           ; Protection for pages $B8 - $BF
        cmp     #%00000001      ; Exactly system global page is protected
        bne     basic

        ; No BASIC.SYSTEM; so, quit to the ProDOS dispatcher instead.
        lda     #<quit
        ldx     #>quit
        sta     done+1
        stx     done+2

        ; No BASIC.SYSTEM; so, use the addr of the ProDOS system global page.
        lda     #<$BF00
        ldx     #>$BF00
        bne     :+              ; Branch always

        ; Get the highest available mem addr from the BASIC interpreter.
basic:  lda     HIMEM
        ldx     HIMEM+1

        ; Set up the C stack.
:       sta     sp
        stx     sp+1

        ; ProDOS TechRefMan, chapter 5.3.5:
        ; "Your system program should place in the RESET vector the
        ;  address of a routine that ... closes the files."
        ldx     #<_exit
        lda     #>_exit
        jsr     reset           ; Setup RESET vector

        ; Call the module constructors.
        jsr     initlib

        ; Switch in LC bank 2 for W/O.
        bit     $C081
        bit     $C081

        ; Copy the LC memory area to the language card. SwiftII fork: cc65's
        ; __fastcall__ _memcpy in place of `jsr $D39A` (Applesoft BLTU2) so we
        ; never touch the Applesoft ROM (absent on the original Integer-BASIC
        ; Apple ][). dest + src are pushed, n goes in A/X; _memcpy no-ops n == 0
        ; (empty launcher LC) and ignores the returned dest.
        lda     #<__LC_START__                          ; dest = LC run addr
        ldx     #>__LC_START__
        jsr     pushax
        lda     #<(__ONCE_LOAD__ + __ONCE_SIZE__)       ; src  = LC load image
        ldx     #>(__ONCE_LOAD__ + __ONCE_SIZE__)
        jsr     pushax
        lda     #<(__LC_LAST__ - __LC_START__)          ; n    = LC size
        ldx     #>(__LC_LAST__ - __LC_START__)
        jsr     _memcpy

        ; Switch in LC bank 2 for R/O and return.
        bit     $C080
        rts

; ------------------------------------------------------------------------

        .code

        ; Set up the RESET vector.
reset:  stx     SOFTEV
        sta     SOFTEV+1
        eor     #$A5
        sta     PWREDUP
return: rts

        ; Quit to the ProDOS dispatcher.
quit:   jsr     $BF00           ; MLI call entry point
        .byte   $65             ; Quit
        .word   q_param

; ------------------------------------------------------------------------

        .rodata

        ; MLI parameter list for quit
q_param:.byte   $04             ; param_count
        .byte   $00             ; quit_type
        .word   $0000           ; reserved
        .byte   $00             ; reserved
        .word   $0000           ; reserved

; ------------------------------------------------------------------------

        .data

        ; Final jump when we're done
done:   jmp     DOSWARM         ; Potentially patched at runtime

; ------------------------------------------------------------------------

        .segment        "INIT"

zpsave: .res    zpspace
rvsave: .res    3
