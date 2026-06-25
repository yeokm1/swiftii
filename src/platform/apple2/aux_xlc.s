; src/platform/apple2/aux_xlc.s — SWIFTAUX aux copy-down trampoline.
;
; Companion design doc:
; docs/contributing/design/011-extras-lc-in-saturn-aux.md § "Stage 2 refresh —
; SWIFTAUX". Linked only into the SWIFTAUX build
; (-DWITH_SWIFTAUX); SWIFTSAT uses xlc.s instead, lite/extras have no
; XLC path at all.
;
; ----------------------------------------------------------------
; The aux model (why this differs from SWIFTSAT's xlc.s):
;
; A //e with a 64K extended-80-col card has 48K of aux main RAM at
; $0200-$BFFF, but cc65 code can't EXECUTE there in place: turning on
; RAMRD to fetch instructions from aux also redirects every data read
; in $0200-$BFFF to aux, so a dispatcher's loads of s_stack[]/vm_sp/
; heap cells (all in MAIN) would read garbage. (And the aux language
; card is gated by ALTZP, which swaps ZP + the 6502 stack.) So the XLC
; bodies are PARKED in aux main RAM and COPIED DOWN one-at-a-time into
; a fixed MAIN-RAM staging buffer, then JSR'd there with all aux
; switches OFF — so the body runs against the normal main ZP, stack,
; and data, exactly like a MAIN-resident function (the slice-0
; invariant, proven 2026-05-30).
;
; The copy uses the //e ROM routine AUXMOVE ($C311) rather than a
; hand-rolled loop: a hand-rolled aux->main copy needs RAMRD on to read
; the aux source, but RAMRD also redirects the copy loop's OWN
; instruction fetches (it lives in MAIN $2xxx) to aux — it would run
; off into aux garbage. AUXMOVE lives in ROM (unaffected by RAMRD/
; RAMWRT) and toggles the switches around each byte internally.
;
; ----------------------------------------------------------------
; Park layout (slice 3 step 2: packed bodies + a directory — no padding,
; no fixed stride, so a body may be any size that fits the STAGING hole):
;
;   aux $2000 (AUX_PARK) : directory — 25 entries (one per builtin id
;                          $0D..$25), 4 bytes each = 100 B. Entry idx =
;                          id - BUILTIN_XLC_FIRST is [off_lo, off_hi,
;                          len_lo, len_hi]; off is relative to AUX_PARK.
;                          Builtins that GROUP (the 14 platform builtins
;                          fan into two bodies) simply share an entry —
;                          the directory subsumes the old id->slot map.
;   aux $2064+           : the distinct body images, concatenated in
;                          pack order, no padding.
;
; The trampoline reads dir[idx] (a 4-byte AUXMOVE), then AUXMOVEs exactly
; `len` body bytes from AUX_PARK+off down to __STAGING__ and JSRs it.
; Copying only the real body size (vs a fixed 2 KB stride) keeps the
; per-call cost proportional to the body and removes the stride ceiling
; (the GR group body is ~2.6 KB). pack_swiftaux.py builds the directory +
; bodies; swiftaux-system.cfg links every body to RUN at __STAGING__ and
; caps each at __STAGEMAX__ (the STAGING..C-stack hole).
;
; KEEP IN SYNC: this AUX_PARK + the 25/4-byte directory shape, the cfg's
; __STAGING__/__STAGEMAX__, pack_swiftaux.py's directory builder + id->body
; map, and the boot launcher's aux loader park base.
;
; ----------------------------------------------------------------
; C-callable entry points:
;
;   void __fastcall__ aux_init(void)
;       Reserved init hook (peer to SWIFTSAT's xlc_init); currently a
;       no-op. The trampoline toggles INTCXROM around its own AUXMOVE
;       call (see below), so there is no global $Cxxx state to set up
;       here — and NOT touching CXROM globally keeps slot-card ROM
;       visible for any later ProDOS/MLI use (file mode).
;
;   swiftii_err_t __fastcall__ aux_xlc_call(uint8_t id)
;       Generic aux dispatch. Caller pre-stores argc to _xlc_argc
;       (builtins_xlc.c), then calls with A = builtin id. Steps:
;         1. idx = id - BUILTIN_XLC_FIRST.
;         2. AUXMOVE the 4-byte directory entry at AUX_PARK + idx*4
;            (aux -> the MAIN dir_ent scratch).
;         3. src = AUX_PARK + off (off = dir_ent[0..1]); A2 = src + len - 1
;            (len = dir_ent[2..3]); A4 = STAGING. AUXMOVE the body
;            (aux -> main). The body now sits at STAGING.
;         4. lda _xlc_argc (A = first arg for the body's fastcall).
;         5. jsr STAGING -> the copied-down dispatcher. Returns
;            A = swiftii_err_t (X = high byte), which we propagate.
;       Each AUXMOVE toggles INTCXROM in only for the ROM call (carry
;       clear = aux -> main).

BUILTIN_XLC_FIRST = $0D                  ; must match opcodes.h

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

AUX_PARK_HI = $20                        ; AUX_PARK = $2000 (aux main RAM)
; The directory sits at AUX_PARK (24 entries x 4 B); off in each entry is
; relative to AUX_PARK, so a body's aux address is AUX_PARK + off.

.export _aux_init
.export _aux_xlc_call
.import _xlc_argc                        ; MAIN BSS (builtins_xlc.c)
.import __STAGING__                       ; overlay run address (cfg)

.segment "CODE"

; ------------------------------------------------------------
; void __fastcall__ aux_init(void)
; ------------------------------------------------------------
_aux_init:
        rts                              ; reserved; trampoline is self-contained

; ------------------------------------------------------------
; swiftii_err_t __fastcall__ aux_xlc_call(uint8_t id)
; cc65 fastcall: A = id on entry; argc in _xlc_argc. Returns A = err.
; ------------------------------------------------------------
_aux_xlc_call:
        ; idx = id - FIRST, the directory entry index (one per builtin id).
        sec
        sbc     #BUILTIN_XLC_FIRST
        asl                              ; idx*2
        asl                              ; idx*4 (4-byte dir entries; max idx
                                         ;  24 -> 96, no page carry)
        ; --- A1 = PARK + idx*4 (dir entry source, aux) ---
        sta     A1L
        lda     #AUX_PARK_HI             ; PARK = $2000
        sta     A1H
        ; --- A2 = A1 + 3 (a 4-byte directory entry) ---
        lda     A1L
        clc
        adc     #3
        sta     A2L
        lda     A1H
        adc     #0
        sta     A2H
        ; --- A4 = dir_ent (main-RAM scratch) ---
        lda     #<dir_ent
        sta     A4L
        lda     #>dir_ent
        sta     A4H
        ; --- AUXMOVE the directory entry: aux -> main (carry clear). ---
        ; Toggle internal $Cxxx ROM in only across the ROM call so MLI's
        ; slot-card ROM stays visible everywhere else.
        sta     SETINTCXROM              ; A ignored (soft switch)
        clc
        jsr     AUXMOVE
        sta     SETSLOTCXROM
        ; dir_ent now = [off_lo, off_hi, len_lo, len_hi], off relative to
        ; PARK base. Compute the body's aux source + main destination.
        ; --- A1 = PARK + off (body source, aux) ---
        lda     dir_ent+0
        sta     A1L
        lda     dir_ent+1
        clc
        adc     #AUX_PARK_HI
        sta     A1H
        ; --- A2 = A1 + len - 1 (body end, inclusive) ---
        lda     A1L
        clc
        adc     dir_ent+2
        sta     A2L
        lda     A1H
        adc     dir_ent+3
        sta     A2H
        lda     A2L                      ; -1 (len is a count; end is inclusive)
        sec
        sbc     #1
        sta     A2L
        lda     A2H
        sbc     #0
        sta     A2H
        ; --- A4 = STAGING (main-RAM destination) ---
        lda     #<__STAGING__
        sta     A4L
        lda     #>__STAGING__
        sta     A4H
        ; --- AUXMOVE the body: aux -> main (carry clear). ---
        sta     SETINTCXROM
        clc
        jsr     AUXMOVE
        sta     SETSLOTCXROM
        ; --- Run the copied-down body with A = argc ---
        lda     _xlc_argc
        jsr     __STAGING__              ; -> dispatcher; returns A = err
        rts

.segment "BSS"
; 4-byte scratch the directory entry [off_lo, off_hi, len_lo, len_hi] is
; AUXMOVE'd into before the body copy. MAIN-resident (the trampoline runs
; in MAIN); zero-init is irrelevant (always overwritten before use).
dir_ent: .res 4
