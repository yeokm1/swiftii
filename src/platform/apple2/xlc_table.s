; src/platform/apple2/xlc_table.s — XLC dispatch table (SWIFTSAT-only).
;
; Companion design doc:
; docs/contributing/design/011-extras-lc-in-saturn-aux.md.
;
; This file's bytes are linked into the SWIFTSAT XLC segment as the
; FIRST .o so its content lands at offset 0 within XLC ($D000 in
; Saturn bank 1). The Makefile passes xlc_table.s ahead of any
; other XLC-contributing input (`builtins_xlc.c` etc.) — verify
; with `grep "00D000" build/apple2/swiftsat/swiftsat.map`; the
; xlc_table symbol should be at $D000.
;
; Layout: a JMP table, one 4-byte slot per XLC builtin (3-byte JMP
; + 1-byte NOP pad), indexed by `(builtin_id - BUILTIN_XLC_FIRST)`.
; The 4-byte stride lets the generic trampoline compute its target
; with two `asl` (slot*4) instead of an `asl + adc <slot>` sequence
; (slot*3) — saves ~4 B in MAIN at the cost of 1 B per slot in XLC,
; where there's 12 KB of headroom. The generic trampoline
; `_call_xlc_dispatch` (xlc.s) does the index math + patches its
; own JSR target before bus-switching; the JSR lands here on a JMP
; that reaches the per-builtin dispatcher. Adding a new XLC
; builtin: bump BUILTIN_XLC_FIRST's allocation in opcodes.h, append
; one `.import` + one `jmp` + `.byte $EA` (NOP) here.

.segment "XLC"

.export xlc_table
.import _xlc_asc_dispatch                ; slot 0 — BUILTIN_ASC ($0D)
.import _xlc_chr_dispatch                ; slot 1 — BUILTIN_CHR ($0E)
.import _xlc_str_concat_dispatch         ; slot 2 — XLC_OP_STR_CONCAT ($0F)
.import _xlc_str_interp_dispatch         ; slot 3 — XLC_OP_STR_INTERP ($10)
.import _xlc_str_to_int_dispatch         ; slot 4 — BUILTIN_STR_TO_INT ($11)
.import _xlc_arr_remove_last_dispatch    ; slot 5 — BUILTIN_ARR_REMOVE_LAST ($12)
.import _xlc_arr_remove_all_dispatch     ; slot 6 — BUILTIN_ARR_REMOVE_ALL ($13)
.import _xlc_arr_contains_dispatch       ; slot 7 — BUILTIN_ARR_CONTAINS ($14)
.import _xlc_new_array_dispatch          ; slot 8 — XLC_OP_NEW_ARRAY ($15)
.import _xlc_arr_len_dispatch            ; slot 9 — XLC_OP_ARR_LEN ($16)
.import _xlc_call_builtin_dispatch       ; slot 10 — XLC_OP_CALL_BUILTIN ($17)
.import _xlc_home_dispatch               ; slot 11 — BUILTIN_HOME ($18)
.import _xlc_peek_dispatch               ; slot 12 — BUILTIN_PEEK ($19)
.import _xlc_poke_dispatch               ; slot 13 — BUILTIN_POKE ($1A)
.import _xlc_htab_dispatch               ; slot 14 — BUILTIN_HTAB ($1B)
.import _xlc_vtab_dispatch               ; slot 15 — BUILTIN_VTAB ($1C)
.import _xlc_gr_dispatch                 ; slot 16 — BUILTIN_GR ($1D)
.import _xlc_text_dispatch               ; slot 17 — BUILTIN_TEXT ($1E)
.import _xlc_color_dispatch              ; slot 18 — BUILTIN_COLOR ($1F)
.import _xlc_plot_dispatch               ; slot 19 — BUILTIN_PLOT ($20)
.import _xlc_gr_full_dispatch            ; slot 20 — BUILTIN_GR_FULL ($21)
.import _xlc_hlin_dispatch               ; slot 21 — BUILTIN_HLIN ($22)
.import _xlc_vlin_dispatch               ; slot 22 — BUILTIN_VLIN ($23)
.import _xlc_scrn_dispatch               ; slot 23 — BUILTIN_SCRN ($24)
.import _xlc_text80_dispatch             ; slot 24 — BUILTIN_TEXT80 ($25, Videx)
.import _xlc_repl_readline_dispatch      ; slot 25 — XLC_OP_REPL_READLINE ($26)

xlc_table:
        jmp     _xlc_asc_dispatch        ; $D000 — BUILTIN_ASC
        .byte   $EA                      ; pad to 4 B
        jmp     _xlc_chr_dispatch        ; $D004 — BUILTIN_CHR
        .byte   $EA                      ; pad to 4 B
        jmp     _xlc_str_concat_dispatch ; $D008 — XLC_OP_STR_CONCAT
        .byte   $EA                      ; pad to 4 B
        jmp     _xlc_str_interp_dispatch ; $D00C — XLC_OP_STR_INTERP
        .byte   $EA                      ; pad to 4 B
        jmp     _xlc_str_to_int_dispatch ; $D010 — BUILTIN_STR_TO_INT
        .byte   $EA                      ; pad to 4 B
        jmp     _xlc_arr_remove_last_dispatch ; $D014 — BUILTIN_ARR_REMOVE_LAST
        .byte   $EA                      ; pad to 4 B
        jmp     _xlc_arr_remove_all_dispatch  ; $D018 — BUILTIN_ARR_REMOVE_ALL
        .byte   $EA                      ; pad to 4 B
        jmp     _xlc_arr_contains_dispatch    ; $D01C — BUILTIN_ARR_CONTAINS
        .byte   $EA                      ; pad to 4 B
        jmp     _xlc_new_array_dispatch       ; $D020 — XLC_OP_NEW_ARRAY
        .byte   $EA                      ; pad to 4 B
        jmp     _xlc_arr_len_dispatch         ; $D024 — XLC_OP_ARR_LEN
        .byte   $EA                      ; pad to 4 B
        jmp     _xlc_call_builtin_dispatch    ; $D028 — XLC_OP_CALL_BUILTIN
        .byte   $EA                      ; pad to 4 B
        jmp     _xlc_home_dispatch            ; $D02C — BUILTIN_HOME
        .byte   $EA                      ; pad to 4 B
        jmp     _xlc_peek_dispatch            ; $D030 — BUILTIN_PEEK
        .byte   $EA                      ; pad to 4 B
        jmp     _xlc_poke_dispatch            ; $D034 — BUILTIN_POKE
        .byte   $EA                      ; pad to 4 B
        jmp     _xlc_htab_dispatch            ; $D038 — BUILTIN_HTAB
        .byte   $EA                      ; pad to 4 B
        jmp     _xlc_vtab_dispatch            ; $D03C — BUILTIN_VTAB
        .byte   $EA                      ; pad to 4 B
        jmp     _xlc_gr_dispatch              ; $D040 — BUILTIN_GR
        .byte   $EA                      ; pad to 4 B
        jmp     _xlc_text_dispatch            ; $D044 — BUILTIN_TEXT
        .byte   $EA                      ; pad to 4 B
        jmp     _xlc_color_dispatch           ; $D048 — BUILTIN_COLOR
        .byte   $EA                      ; pad to 4 B
        jmp     _xlc_plot_dispatch            ; $D04C — BUILTIN_PLOT
        .byte   $EA                      ; pad to 4 B
        jmp     _xlc_gr_full_dispatch         ; $D050 — BUILTIN_GR_FULL
        .byte   $EA                      ; pad to 4 B
        jmp     _xlc_hlin_dispatch            ; $D054 — BUILTIN_HLIN
        .byte   $EA                      ; pad to 4 B
        jmp     _xlc_vlin_dispatch            ; $D058 — BUILTIN_VLIN
        .byte   $EA                      ; pad to 4 B
        jmp     _xlc_scrn_dispatch            ; $D05C — BUILTIN_SCRN
        .byte   $EA                      ; pad to 4 B
        jmp     _xlc_text80_dispatch          ; $D060 — BUILTIN_TEXT80 (Videx)
        .byte   $EA                      ; pad to 4 B
        jmp     _xlc_repl_readline_dispatch   ; $D064 — XLC_OP_REPL_READLINE
        .byte   $EA                      ; pad to 4 B
