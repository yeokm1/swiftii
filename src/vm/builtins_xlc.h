/* Declarations for XLC-resident built-in workers + dispatchers.
 *
 * S 3b-3d:
 *   3b: `xlc_asc(payload)` worker landed in XLC.
 *   3c: `xlc_asc_dispatch(argc)` wrapper took on the full VM case
 *       body; vm.c's case shrank to a single dispatch call.
 *   3d: generic dispatch — one trampoline (`call_xlc_dispatch(id)`)
 *       + one JMP table at XLC offset 0 (xlc_table.s) replace the
 *       per-builtin trampolines. New XLC builtins cost just a
 *       .word entry in the table; MAIN's per-builtin overhead is
 *       only the parser branch in builtin_calls.c.
 *
 * See builtins_xlc.c for bodies and design doc 011 for the XLC
 * mechanism. */
#ifndef SWIFTII_BUILTINS_XLC_H
#define SWIFTII_BUILTINS_XLC_H

#include <stdint.h>
#include "../common/errors.h"

#if defined(WITH_SWIFTSAT) || defined(WITH_SWIFTAUX) || defined(WITH_SWB) || \
    !defined(__CC65__)

/* Inner worker: returns the first byte of the string at `payload`
 * as 0..255, or -1 if the string is empty / payload is invalid.
 * Pure function, separately unit-testable on host. */
int16_t xlc_asc(uint16_t payload);

/* Full BUILTIN_ASC dispatch: validates argc, pops the T_STR
 * argument, calls xlc_asc, releases the heap-ref, pushes the T_INT
 * result. Returns SE_OK on success, SE_BAD_OPCODE for wrong argc,
 * SE_TYPE_MISMATCH for non-string arg or empty string. Reads +
 * writes the MAIN-resident s_stack / vm_sp pair directly. */
swiftii_err_t xlc_asc_dispatch(uint8_t argc);

/* Full BUILTIN_CHR dispatch: validates
 * argc, pops the T_INT argument, allocates a 1-byte heap String
 * holding that byte, pushes the T_STR result. Returns SE_OK on
 * success, SE_BAD_OPCODE for wrong argc, SE_TYPE_MISMATCH for a
 * non-int arg, SE_RUNTIME if the int is outside 0..255, SE_OOM if
 * the heap is full. Reads + writes the MAIN-resident s_stack / vm_sp
 * pair directly. Inverse of xlc_asc_dispatch. */
swiftii_err_t xlc_chr_dispatch(uint8_t argc);

/* Relocated OP_STR_CONCAT body (XLC opcode-dispatch prototype, not a
 * user builtin). Pops two T_STR operands, pushes their heap-String
 * concatenation. `argc` unused. Returns SE_OK, SE_STACK_UNDER if
 * fewer than two operands, SE_TYPE_MISMATCH for non-strings, SE_OOM
 * if the result exceeds 256 B or the heap is full. Reached via
 * XLC_CALL(XLC_OP_STR_CONCAT, 0) from vm.c. */
swiftii_err_t xlc_str_concat_dispatch(uint8_t argc);

/* `Int(_ s: String) -> Int?` worker: parse a decimal string (optional
 * leading +/-) into an int16. Returns 1 + writes *out on success, 0 on
 * empty / sign-only / non-digit / out-of-range. Pure, unit-testable. */
uint8_t xlc_str_to_int(uint16_t payload, int16_t *out);

#if defined(WITH_SWB) || !defined(__CC65__)
/* hasPrefix / hasSuffix worker (Family B Runner + host): returns 1 if the
 * String at `recv_payload` starts with (is_suffix=0) / ends with
 * (is_suffix=1) the String at `needle_payload`, else 0. An empty needle
 * always matches (Swift semantics). Pure, separately unit-testable;
 * shared by the Runner inline dispatch (vm.c) and the host dispatch
 * below so the byte-compare logic has one home. */
int16_t xlc_has_affix(uint16_t recv_payload, uint16_t needle_payload,
                      unsigned char is_suffix);
#endif

/* Full BUILTIN_STR_TO_INT dispatch: pops the T_STR arg, pushes T_INT
 * (some) or T_OPT_NIL (none). Returns SE_OK, SE_BAD_OPCODE for wrong
 * argc, SE_TYPE_MISMATCH for a non-string arg. A failed parse is a
 * valid nil result, not an error. */
swiftii_err_t xlc_str_to_int_dispatch(uint8_t argc);

/* Relocated OP_STR_INTERP_I body (XLC opcode dispatch). Converts the
 * TOS int/bool/nil to a heap String in place; T_STR passes through.
 * `argc` unused. Returns SE_OK, SE_STACK_UNDER if empty,
 * SE_TYPE_MISMATCH for unsupported tags, SE_OOM on heap exhaustion.
 * Reached via XLC_CALL(XLC_OP_STR_INTERP, 0) from vm.c. */
swiftii_err_t xlc_str_interp_dispatch(uint8_t argc);

/* Array methods (extras), emitted as OP_CALL_BUILTIN
 * from the Pratt postfix dot-member handler with the receiver array on
 * the stack. All three mutate the array in place (same heap offset) so
 * the source variable needs no write-back.
 *   removeLast ( arr -- elem )  argc=1; SE_RUNTIME on empty
 *   removeAll  ( arr -- nil )   argc=1
 *   contains   ( arr v -- bool ) argc=2; OP_EQ value equality */
swiftii_err_t xlc_arr_remove_last_dispatch(uint8_t argc);
swiftii_err_t xlc_arr_remove_all_dispatch(uint8_t argc);
swiftii_err_t xlc_arr_contains_dispatch(uint8_t argc);

/* Relocated cold opcode bodies (not user builtins). OP_NEW_ARRAY takes
 * the literal element count via `argc`; OP_ARR_LEN ignores argc. Both
 * read/write the MAIN-resident s_stack / vm_sp. Reached via
 * XLC_CALL(XLC_OP_NEW_ARRAY, n) / XLC_CALL(XLC_OP_ARR_LEN, 0) from
 * vm.c; kept byte-equivalent to the inline copies there for lite. */
swiftii_err_t xlc_new_array_dispatch(uint8_t argc);
swiftii_err_t xlc_arr_len_dispatch(uint8_t argc);

/* Relocated OP_CALL_BUILTIN core bodies (print / print_t / readLine /
 * min / max). The owning vm.c case routes here for every builtin id <
 * BUILTIN_XLC_FIRST on SWIFTSAT/SWIFTAUX/host; the specific id arrives via the
 * xlc_builtin_id transport (below), argc via the usual slot. Returns
 * the same codes the inline vm.c copy does. Kept byte-equivalent to
 * that copy for lite. */
swiftii_err_t xlc_call_builtin_dispatch(uint8_t argc);

/* Platform builtins (extras). All push a result so
 * the expr-statement layer's OP_POP has something to discard.
 *   home ( -- nil )          argc=0; clears the text screen
 *   peek ( addr -- byte )    argc=1; T_INT 0..255 (cc65 reads RAM; host 0)
 *   poke ( addr val -- nil )  argc=2; cc65 writes RAM, host no-op
 * SE_BAD_OPCODE on wrong argc, SE_TYPE_MISMATCH on a non-int arg. */
swiftii_err_t xlc_home_dispatch(uint8_t argc);
swiftii_err_t xlc_peek_dispatch(uint8_t argc);
swiftii_err_t xlc_poke_dispatch(uint8_t argc);

/* Cursor positioning (extras).
 *   htab ( col -- nil )  argc=1; 1-based column 1..40
 *   vtab ( row -- nil )  argc=1; 1-based row 1..24
 * SE_BAD_OPCODE on wrong argc, SE_TYPE_MISMATCH on a non-int arg,
 * SE_RUNTIME if the position is out of range. Route through
 * platform_htab/vtab (cc65 gotoxy; host no-op). */
swiftii_err_t xlc_htab_dispatch(uint8_t argc);
swiftii_err_t xlc_vtab_dispatch(uint8_t argc);

/* Low-res graphics (extras).
 *   gr    ( -- nil )        argc=0; enter mixed GR, clear, colour=0
 *   text  ( -- nil )        argc=0; back to 40-col text, clear
 *   color ( n -- nil )      argc=1; set current colour, SE_RUNTIME if !0..15
 *   plot  ( x y -- nil )    argc=2; block at (x,y), SE_RUNTIME out of range
 * SE_BAD_OPCODE on wrong argc, SE_TYPE_MISMATCH on a non-int arg. Route
 * through platform_gr/text/gr_color/gr_plot. */
swiftii_err_t xlc_gr_dispatch(uint8_t argc);
swiftii_err_t xlc_gr_full_dispatch(uint8_t argc);
swiftii_err_t xlc_text_dispatch(uint8_t argc);
swiftii_err_t xlc_color_dispatch(uint8_t argc);
swiftii_err_t xlc_plot_dispatch(uint8_t argc);

/* GR line draw + read (extras).
 *   hlin ( x1 x2 y -- nil )  argc=3; horizontal run, current colour
 *   vlin ( y1 y2 x -- nil )  argc=3; vertical run
 *   scrn ( x y -- colour )   argc=2; read colour 0..15 (host 0)
 * Endpoints may be in either order. SE_RUNTIME out of range. */
swiftii_err_t xlc_hlin_dispatch(uint8_t argc);
swiftii_err_t xlc_vlin_dispatch(uint8_t argc);
swiftii_err_t xlc_scrn_dispatch(uint8_t argc);

#if defined(WITH_SWIFTAUX) || !defined(__CC65__)
/* `Text80()` switches the //e console to 80 columns.
 * No-op on SWIFTSAT/host (no //e firmware path); shares the SWIFTAUX
 * XLCPGR copy-down overlay. */
swiftii_err_t xlc_text80_dispatch(uint8_t argc);
#endif

/* MAIN-side transport for the relocated OP_CALL_BUILTIN's builtin id
 * (the dispatch arg slot already carries argc). Set by vm.c right
 * before XLC_CALL(XLC_OP_CALL_BUILTIN, argc); MAIN BSS, same as
 * xlc_argc. */
extern uint8_t xlc_builtin_id;

/* MAIN-side argc transport for the generic XLC trampoline (cc65
 * fastcall passes one byte in A; we use this slot for the second
 * arg). Set by the XLC_CALL macro in vm.c immediately before
 * call_xlc_dispatch(id); the asm trampoline reloads A from it just
 * before JSR'ing the per-builtin dispatcher. Lives in MAIN BSS on
 * cc65, regular global on host. */
extern uint8_t xlc_argc;

#if !defined(__CC65__) || defined(WITH_SWB)
/* Plain-C dispatch switch: parallels the SWIFTSAT `call_xlc_dispatch(id)`
 * + the JMP table in xlc_table.s. Dispatches by id to the right
 * per-builtin xlc_*_dispatch worker. vm.c's XLC_CALL macro routes here
 * on host; the Family B Runner (cc65, WITH_SWB) calls it directly from
 * the lite OP_CALL_BUILTIN path for ids >= BUILTIN_XLC_FIRST. */
swiftii_err_t xlc_call_dispatch(uint8_t id, uint8_t argc);
#endif

#endif

#endif /* SWIFTII_BUILTINS_XLC_H */
