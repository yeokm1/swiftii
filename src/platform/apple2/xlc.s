; src/platform/apple2/xlc.s — XLC runtime trampoline (SWIFTSAT-only).
;
; Companion design doc:
; docs/contributing/design/011-extras-lc-in-saturn-aux.md.
;
; This file is linked only into the SWIFTSAT build (Saturn 128K
; extras). Lite builds don't include it.
;
; XLC ("extras LC") is the code segment that lives in
; Saturn 16K bank 1 at $D000+, separate physical RAM from the
; LC-compat bank 0 (where MLI body and cc65 LC live). At runtime
; the trampoline below switches Saturn to bank 1 to JSR into XLC,
; then back to bank 0 so the next ROM/LC call sees the right code.
;
; Commit 3d (2026-05-29) collapsed the per-builtin trampolines
; from commits 3b/3c into a single generic dispatch — vm.c routes
; all builtin ids ≥ BUILTIN_XLC_FIRST through `_call_xlc_dispatch`,
; which patches its own JSR target based on the id and lands on
; the JMP table at $D000 (xlc_table.s). Adding a new XLC builtin
; now costs zero bytes in this file.
;
; ----------------------------------------------------------------
; Saturn 128K bank mapping (per AppleWin LanguageCard.cpp formula
; `bank = ((addr>>1)&4) | (addr&3)`):
;
;   $C0(slot+8)0..3  R/W mode for 4K bank A (LC bank 2 convention)
;   $C0(slot+8)4     select Saturn bank 0  (LC-compat, MLI body)
;   $C0(slot+8)5     select Saturn bank 1  (first extras bank — XLC)
;   $C0(slot+8)6     select Saturn bank 2
;   $C0(slot+8)7     select Saturn bank 3
;   $C0(slot+8)8..B  R/W mode for 4K bank B (LC bank 1 convention)
;   $C0(slot+8)C..F  select Saturn banks 4..7
;
; Each Saturn bank is fully independent 16K physical storage (no
; shared upper region). 8 banks × 16K = 128 KB total. Bank
; selection is independent of R/W mode (selecting a bank doesn't
; touch the mode bits and vice versa). Initial state at power-on:
; bank 0, R-RAM bank A, write-enabled (per AppleWin's
; `kMemModeInitialState = MF_BANK2 | MF_WRITERAM`).
;
; ----------------------------------------------------------------
; C-callable entry points:
;
;   void __fastcall__ xlc_init(uint8_t saturn_slot)
;       Called once at main() entry. Patches three switch addresses
;       in the trampoline based on the detected Saturn slot (the
;       boot launcher deposited the slot byte at SX_SAT_SLOT = $1B04
;       before LOADER ran), and leaves Saturn in R-RAM bank A,
;       write-protect mode so stray writes from main code can't
;       corrupt bank 0's contents (cc65 LC code). slot = $FF (no
;       Saturn) is a no-op — this file is linked only into SWIFTSAT,
;       which the boot launcher chains only on a Saturn machine. Aux-only
;       machines run SWIFTAUX instead (its own aux copy-down trampoline
;       in aux_xlc.s; this Saturn file isn't compiled there).
;
;   swiftii_err_t __fastcall__ call_xlc_dispatch(uint8_t id)
;       Generic dispatch trampoline. Caller pre-stores argc to the
;       MAIN BSS slot `_xlc_argc` (builtins_xlc.c), then calls with
;       A = builtin id. The dance:
;         1. Subtract BUILTIN_XLC_FIRST from id → slot index.
;         2. Multiply slot by 3 → offset into the JMP table at
;            $D000 (xlc_table.s).
;         3. Self-modify the JSR's low byte (high stays $D0 since
;            the table is at $D000 and a 32-slot table fits in one
;            page).
;         4. sat_cede_builtin: bit $C080 (slot 0) or $C082 (slot N≠0).
;            On slot N, this puts the separate built-in LC in
;            ROM-read mode so it cedes the $D000+ bus to Saturn slot
;            N (per docs/contributing/LESSONS.md 2026-05-26: on Mariani //e, built-in
;            LC wins bus arbitration when both are R-RAM-enabled).
;            On slot 0, Saturn IS the LC — there's no separate
;            built-in to cede; $C080 just sets R-RAM bank A, WP
;            mode (idempotent with xlc_init's lockdown).
;         5. sat_sel_bank1: bit $C0(slot+8)5 — Saturn to bank 1
;            (XLC visible at $D000+). R/W mode preserved.
;         6. lda _xlc_argc — reload argc for the per-builtin
;            dispatcher's cc65 fastcall convention (A = first arg).
;         7. jsr table_target → JMP at $D000+offset → dispatcher.
;            Dispatcher returns A = swiftii_err_t.
;         8. sat_sel_bank0: bit $C0(slot+8)4 — Saturn back to bank
;            0 (LC-compat / cc65 LC restored).
;         9. bit $C080 — on slot N, built-in to bank 2 R/O (wins
;            arbitration again, cc65 LC visible). On slot 0,
;            Saturn R-RAM bank A, WP (cc65 LC visible in bank 0).
;       BIT preserves A across all the switch instructions, so the
;       dispatcher's err propagates unchanged to the C caller.
;
; ----------------------------------------------------------------
; Why slot-conditional patching for sat_cede_builtin:
;
; On Saturn-at-slot-N (N≠0): there are two physical LCs — the
; built-in 16K LC at $C080-$C08F and Saturn at $C0(N+8)0-F. We
; need to take built-in OFF the $D000+ bus (so Saturn drives reads)
; via $C082 (built-in to ROM-read).
;
; On Saturn-at-slot-0: Saturn IS the built-in LC. The "built-in's
; $C082" and "Saturn's $C082" are the same switch — and it puts
; Saturn itself in R-ROM mode, hiding the RAM (including our XLC).
; We need Saturn to STAY in R-RAM mode so $D000+ shows our bytes.
; Easiest fix: skip the cede, since there's no second LC to fight
; with. We achieve "skip" by patching sat_cede_builtin to do
; `bit $C080` (R-RAM bank A, WP) which is the desired state anyway.

BUILTIN_XLC_FIRST = $0D                  ; must match opcodes.h

.export _xlc_init
.export _call_xlc_dispatch
.import _xlc_argc                        ; MAIN BSS (builtins_xlc.c)

; ============================================================
.segment "CODE"

; ------------------------------------------------------------
; void __fastcall__ xlc_init(uint8_t saturn_slot)
; cc65 fastcall: A = saturn_slot on entry. Clobbers A, X, Y, P.
; ------------------------------------------------------------
_xlc_init:
        cmp     #$FF
        beq     xlc_init_done            ; no Saturn — no patching
        tax                              ; X = slot (for the slot-0 test below)

        ; Compute slot-base low byte = (slot+8) * 16. For slot N,
        ; that's $80 + N*16 = the standard "slot N I/O" low byte.
        clc
        adc     #8
        asl
        asl
        asl
        asl                              ; A = (slot+8)*16 = slot-base low

        pha                              ; stash slot-base #1

        ; --- Patch call_xlc_dispatch's bank-1 select switch ---
        ora     #$05                     ; $C0(slot+8)5 = bank 1 select
        sta     sat_sel_bank1+1

        ; --- Patch call_xlc_dispatch's bank-0 restore switch ---
        pla
        pha                              ; restash slot-base #2
        ora     #$04                     ; $C0(slot+8)4 = bank 0 select
        sta     sat_sel_bank0+1

        ; --- Patch the cede-builtin switch (slot-conditional) ---
        ; Template value is $C080 (correct for slot 0). For slot N≠0
        ; we patch the low byte to $82 so the BIT instruction becomes
        ; `bit $C082` (built-in LC → ROM-read, cedes the bus).
        txa
        beq     skip_cede_patch          ; slot 0 → keep template $80
        lda     #$82
        sta     sat_cede_builtin+1
skip_cede_patch:

        ; --- Lock Saturn down: R-RAM bank A, write-protect ---
        ; xlc_init runs once at main() entry. cc65's crt0 just left
        ; Saturn in bank 0 R-RAM bank A, WP (its idle state via
        ; $C080). On slot N (N≠0), built-in is in that state and
        ; Saturn slot N is in whatever the LOADER left it (bank 0,
        ; R-RAM bank A, WE leftover from the memcpy). Flip
        ; Saturn slot N to WP so stray writes from main code can't
        ; corrupt bank 0's contents. On slot 0, this is a no-op
        ; (already in this state).
        pla                              ; recover slot-base
        ora     #$00                     ; R-RAM bank A, WP offset (0)
        sta     init_sat_lock+1
init_sat_lock:
        bit     $C0F0                    ; patched: $C0(slot+8)0
xlc_init_done:
        rts

; ------------------------------------------------------------
; swiftii_err_t __fastcall__ call_xlc_dispatch(uint8_t id)
; cc65 fastcall: A = builtin id on entry. argc must be stored in
; _xlc_argc by the caller (vm.c's XLC_CALL macro). Returns A =
; swiftii_err_t (single byte; the dispatcher reads + writes the
; MAIN s_stack[]/vm_sp pair directly).
;
; The id-to-offset math runs in MAIN (always-visible code) before
; the bus switches; only the JSR itself and the dispatcher body
; execute against Saturn bank 1.
; ------------------------------------------------------------
_call_xlc_dispatch:
        sec
        sbc     #BUILTIN_XLC_FIRST       ; A = slot index (0..N-1)
        asl                              ; A = slot * 2
        asl                              ; A = slot * 4 = byte offset
                                         ; (4-byte JMP+NOP slots per
                                         ; xlc_table.s; 256/4 = 64
                                         ; XLC builtins fit one page)
        sta     table_target+1           ; patch JSR low byte
sat_cede_builtin:
        bit     $C080                    ; template: slot 0 (Saturn R-RAM bank A WP)
                                         ; patched: $C082 for slot N (built-in ROM-read)
sat_sel_bank1:
        bit     $C0F5                    ; patched: Saturn bank 1 select
        lda     _xlc_argc                ; reload argc for the dispatcher (A = first arg)
table_target:
        jsr     $D000                    ; patched: $D000 + slot*3 → JMP in xlc_table
sat_sel_bank0:
        bit     $C0F4                    ; patched: Saturn bank 0 select (LC-compat)
        bit     $C080                    ; slot N: built-in to bank 2 R/O.
                                         ; slot 0: Saturn R-RAM bank A WP (no-op).
        rts

