; src/platform/apple2/aux_table.s — SWIFTAUX per-overlay entry stubs.
;
; Linked only into the SWIFTAUX build.
;
; The aux trampoline (aux_xlc.s `aux_xlc_call`) copies one XLC overlay down
; to the staging buffer and `JSR`s the overlay's run address
; (__STAGING__) directly — so the dispatcher must be reachable at offset
; 0 of each overlay. cc65 does NOT guarantee the dispatcher lands first
; in its segment (empirically it emits the asc worker ahead of the asc
; dispatcher), so we can't rely on source order. Instead this file
; contributes a 3-byte `jmp <dispatcher>` at the HEAD of each overlay
; segment; `jsr __STAGING__` then lands on the jmp, which vectors to the
; real dispatcher wherever cc65 placed it within the (copied-down)
; overlay. The jmp target is absolute = the dispatcher's link address
; inside the overlay, which is correct because the whole overlay is
; copied to __STAGING__ before the call.
;
; This file MUST lead the cl65 input order (Makefile
; A2AUX_PLATFORM_ASM_FIRST) so each segment's jmp lands at offset 0 of
; its OVL* output file — exactly the role xlc_table.s plays for SWIFTSAT.

.import _xlc_asc_dispatch
.import _xlc_chr_dispatch
.import _xlc_call_builtin_dispatch
; Slice 3 step 1: Int(s) + the three array methods, each its own overlay,
; plus str_interp evicted from MAIN to its own overlay (slot 3).
.import _xlc_str_to_int_dispatch
.import _xlc_str_interp_dispatch
.import _xlc_arr_remove_last_dispatch
.import _xlc_arr_remove_all_dispatch
.import _xlc_arr_contains_dispatch
; Slice 3 step 2: the 13 platform builtins grouped into two overlays whose
; entry switches on xlc_builtin_id, plus str_concat/new_array/arr_len
; evicted from inline-MAIN to overlays to free the platform parser table's
; MAIN cost. The directory (aux_xlc.s) maps each builtin id to a body.
.import _xlc_pmem_dispatch
.import _xlc_pgr_dispatch
.import _xlc_str_concat_dispatch
.import _xlc_new_array_dispatch
.import _xlc_arr_len_dispatch

.segment "XLCASC"
        jmp     _xlc_asc_dispatch

.segment "XLCCHR"
        jmp     _xlc_chr_dispatch

.segment "XLCCALL"
        jmp     _xlc_call_builtin_dispatch

.segment "XLCSIP"
        jmp     _xlc_str_interp_dispatch

.segment "XLCINT"
        jmp     _xlc_str_to_int_dispatch

.segment "XLCRML"
        jmp     _xlc_arr_remove_last_dispatch

.segment "XLCRMA"
        jmp     _xlc_arr_remove_all_dispatch

.segment "XLCCON"
        jmp     _xlc_arr_contains_dispatch

.segment "XLCPMEM"
        jmp     _xlc_pmem_dispatch

.segment "XLCPGR"
        jmp     _xlc_pgr_dispatch

.segment "XLCSCC"
        jmp     _xlc_str_concat_dispatch

.segment "XLCNAR"
        jmp     _xlc_new_array_dispatch

.segment "XLCALN"
        jmp     _xlc_arr_len_dispatch
