/* XLC-resident built-in helpers.
 *
 * Holds the worker functions AND the full VM dispatch case bodies
 * for cold built-ins that are moved out of MAIN on memory-expanded
 * builds: Saturn-bank-1 XLC on SWIFTSAT, aux copy-down overlays on
 * SWIFTAUX, and normal CODE on Family B / host.
 *
 * Commit 3d (2026-05-29) added the generic dispatch path: vm.c
 * routes every builtin id ≥ BUILTIN_XLC_FIRST through
 * `call_xlc_dispatch(id)` (cc65) / `xlc_call_dispatch(id, argc)`
 * (host); the cc65 path JSRs through a JMP table at XLC offset 0
 * (xlc_table.s) to the per-builtin dispatcher. Adding a new XLC
 * builtin costs one .word in xlc_table.s + one switch case in the
 * host shim below — no new trampolines, no new vm.c case.
 *
 * On cc65, the `#pragma code-name` below places every function in
 * this file into the "XLC" segment, which the SWIFTSAT loader stages
 * into Saturn bank 1 at $D000+ before main() runs. Callers reach
 * `xlc_asc_dispatch` through a bank-switching trampoline in
 * src/platform/apple2/xlc.s; the inner `xlc_asc` worker is only
 * called from inside XLC (xlc_asc_dispatch → xlc_asc), so no
 * trampoline is needed for it.
 *
 * On Family B and host builds, the pragma is a no-op and the functions
 * live in normal CODE. vm.c calls xlc_asc_dispatch directly without a
 * trampoline.
 *
 * On the lite cc65 builds, this file expands to
 * nothing (the `#if` below gates the whole body out). cc65 produces
 * an empty .o, ld65 contributes zero bytes — asc simply isn't
 * available on those binaries, which matches the design intent of
 * extras being absent from Family A lite.
 *
 * Commit 3c (2026-05-28) moved the dispatch case body out of vm.c
 * into xlc_asc_dispatch here, shrinking MAIN per-builtin overhead
 * from ~80-100 B to ~15-25 B. The pattern (split worker from
 * dispatcher, only the dispatcher takes argc + touches the stack)
 * is the template for chr / Int(s) / future XLC builtins.
 */

#if defined(WITH_SWIFTSAT) || defined(WITH_SWIFTAUX) || defined(WITH_SWB) || \
    !defined(__CC65__)

#include <stdint.h>

#include "../common/config.h"
#include "../common/errors.h"
#include "../common/types.h"
#include "../common/zeropage.h"
#include "../runtime/array.h"
#include "../runtime/builtins.h"
#include "../runtime/heap.h"
#include "../runtime/string_pool.h"
#include "../runtime/value.h"
#include "../platform/platform.h"
#include "opcodes.h"
#include "vm.h"
#include "builtins_xlc.h"

#if defined(__CC65__) && defined(WITH_SWIFTSAT)
extern unsigned char g_read_line_in_xlc;
#endif

/* MAIN-resident VM stack — declared non-static in vm.c specifically
 * so the XLC dispatchers below can pop/push directly. vm_sp comes
 * via common/zeropage.h (zero-page on cc65, regular global on host). */
extern Value s_stack[VM_STACK_SLOTS];

/* Transport for the second arg through the generic dispatch
 * trampoline. Lives in MAIN BSS regardless of the code-name pragma
 * below (cc65's #pragma code-name only affects functions). vm.c's
 * XLC_CALL macro writes it, then the asm trampoline reloads A from
 * it just before JSR'ing the per-builtin dispatcher. Defined here
 * so it survives the lite builds (the #if above
 * gates the whole file out on those, so no BSS slot leaks). */
uint8_t xlc_argc;

/* Transport for the relocated OP_CALL_BUILTIN's builtin id (argc rides
 * xlc_argc). MAIN BSS, same rationale as xlc_argc. */
uint8_t xlc_builtin_id;

#if defined(WITH_SWB) || !defined(__CC65__)
/* hasPrefix / hasSuffix worker — normal CODE on the Family B Runner and
 * host (not an XLC/overlay builtin: hasPrefix/hasSuffix are SYS-range
 * Family B builtins, $30/$31). Compares bytes via str_bytes (a JSR back
 * to MAIN is fine from normal CODE). A manual loop avoids a string.h
 * dependency. See builtins_xlc.h for the contract. */
int16_t xlc_has_affix(uint16_t recv_payload, uint16_t needle_payload,
                      unsigned char is_suffix) {
  const unsigned char *rd;
  const unsigned char *nd;
  uint16_t rl;
  uint16_t nl;
  uint16_t off;
  uint16_t i;
  str_bytes(recv_payload, &rd, &rl);
  str_bytes(needle_payload, &nd, &nl);
  if (nl > rl) return 0;
  off = is_suffix ? (uint16_t)(rl - nl) : (uint16_t)0;
  for (i = 0; i < nl; ++i) {
    if (rd[off + i] != nd[i]) return 0;
  }
  return 1;
}
#endif

#if defined(__CC65__) && !defined(WITH_SWB)
#if defined(WITH_SWIFTAUX)
/* Spike (docs/contributing/design/012 stage-2 refresh): the asc
 * worker + dispatcher go into their own XLCASC segment so
 * swiftaux-spike.cfg can link them to run at the main-RAM STAGING
 * address (copy-down model), separate from the rest of the XLC bodies
 * which keep the SWIFTSAT in-place-at-$D000 linkage. The two asc
 * functions must be co-located in one segment because
 * xlc_asc_dispatch JSRs the worker xlc_asc — an intra-blob call that
 * has to resolve to STAGING, not $D000. */
#pragma code-name (push, "XLCASC")
#else
#pragma code-name (push, "XLC")
#endif
#endif
/* Family B Runner (WITH_SWB, cc65): NO code-name pragma — every body in
 * this file links into normal MAIN CODE (swiftii-runner.cfg has no XLC
 * region; the Runner has no bank to park code in, which is the point). */

/* `asc(_ s: String) -> Int` — return the first byte of `s` as 0..255,
 * or -1 if `s` is empty / invalid. The dispatcher below translates
 * -1 into SE_TYPE_MISMATCH; any non-negative value becomes a T_INT
 * pushed on the stack.
 *
 * Pool vs heap is the same payload-vs-STRING_POOL_SLOTS split used
 * by str_bytes() in vm.c. Replicated here instead of exposing
 * str_bytes because XLC code that calls back into MAIN/LC is fine,
 * but a one-liner avoids the JSR + the asymmetry of moving a
 * private helper out of vm.c just for this caller.
 */
int16_t xlc_asc(uint16_t payload) {
  const StringPoolEntry *entry;
  const unsigned char *data;
  uint16_t len;

  if (payload < (uint16_t)STRING_POOL_SLOTS) {
    entry = string_pool_get(payload);
    if (entry == (const StringPoolEntry *)0 || entry->len == 0) {
      return -1;
    }
    return (int16_t)(unsigned char)entry->data[0];
  }
  data = heap_payload((heap_off_t)payload);
  len = heap_len((heap_off_t)payload);
  if (data == (const unsigned char *)0 || len == 0) {
    return -1;
  }
  return (int16_t)data[0];
}

/* Full BUILTIN_ASC VM case body. vm.c's case shrinks to a single
 * `rc = ASC_DISPATCH(argc); if (rc) return rc;`. Owns:
 *   - argc validation (must be 1; the parser always emits 1 today,
 *     but the check is defensive against malformed bytecode)
 *   - T_STR pop from the top of the stack
 *   - call into the inner worker xlc_asc()
 *   - value_release on the consumed string (drops the heap refcount;
 *     topmost-allocation reclaim happens in heap.c)
 *   - empty-string → SE_TYPE_MISMATCH translation
 *   - T_INT result push
 * All stack manipulation runs from XLC against the MAIN-resident
 * s_stack[] + vm_sp pair; main RAM is always visible from XLC code
 * regardless of the Saturn bank-select / aux copy-down state.
 *
 * The dispatcher need not be first in the XLCASC overlay: SWIFTAUX's
 * aux_table.s puts a `jmp _xlc_asc_dispatch` stub at the overlay's
 * offset 0 (where aux_xlc_call JSRs), and SWIFTSAT reaches it by symbol
 * via the xlc_table JMP table — so source order is free, and we keep
 * the worker-then-dispatcher order (byte-identical to earlier). */
swiftii_err_t xlc_asc_dispatch(uint8_t argc) {
  Value va;
  Value v;
  int16_t r;
  if (argc != 1) return SE_BAD_OPCODE;
  va = s_stack[vm_sp - 1];
  if (va.tag != T_STR) return SE_TYPE_MISMATCH;
  r = xlc_asc(((uint16_t)va.hi << 8) | va.lo);
  value_release(&va);
  vm_sp = (uint8_t)(vm_sp - 1);
  if (r < 0) return SE_TYPE_MISMATCH;
  v.tag = T_INT;
  v.lo = (unsigned char)r;
  v.hi = 0;
  s_stack[vm_sp++] = v;
  return SE_OK;
}

#if defined(__CC65__) && defined(WITH_SWIFTAUX)
/* End of the asc overlay body (slice 0). chr is the second SWIFTAUX
 * proof builtin (slice 2): it gets its own XLCCHR overlay so the aux
 * trampoline can copy it down to STAGING independently (overlays at the
 * same run address can't co-exist in one segment). It has no file-local
 * worker, so the dispatcher is the whole overlay. */
#pragma code-name (pop)
#pragma code-name (push, "XLCCHR")
#endif

/* `chr(_ n: Int) -> String` — inverse of asc: a single-character
 * heap String holding the byte `n`. Valid range is 0..255 (a byte);
 * negative or >255 values are SE_RUNTIME (out of range). The byte is
 * stored raw — only 0..127 are standard ASCII on the Apple II text
 * screen, the upper half renders inverse/flashing per the pre-IIe
 * character ROM, matching asc's full 0..255 first-byte return.
 *
 * No separate pure worker (cf. xlc_asc): the only work is the heap
 * allocation, which is intrinsically side-effecting, so the body
 * lives entirely in the dispatcher. The heap-string construction
 * mirrors make_heap_str() in vm.c (which is static there, so it is
 * replicated rather than shared — same rationale as xlc_asc vs
 * str_bytes). The input is a T_INT and holds no heap ref, so unlike
 * xlc_asc_dispatch there is no value_release on the popped arg.
 */
swiftii_err_t xlc_chr_dispatch(uint8_t argc) {
  Value va;
  Value v;
  heap_off_t off;
  unsigned char *dst;
  if (argc != 1) return SE_BAD_OPCODE;
  va = s_stack[vm_sp - 1];
  if (va.tag != T_INT) return SE_TYPE_MISMATCH;
  /* Byte range only: a value of 0..255 has a zero high byte;
   * negatives (hi == 0xFF) and >255 (hi != 0) are out of range. */
  if (va.hi != 0) return SE_RUNTIME;
  off = heap_alloc(1);
  if (off == HEAP_NULL) return SE_OOM;
  dst = heap_payload(off);
  dst[0] = va.lo;
  vm_sp = (uint8_t)(vm_sp - 1);
  v.tag = T_STR;
  v.lo = (unsigned char)((uint16_t)off & 0xFF);
  v.hi = (unsigned char)(((uint16_t)off >> 8) & 0xFF);
  s_stack[vm_sp++] = v;
  return SE_OK;
}

#if defined(__CC65__) && defined(WITH_SWIFTAUX)
/* End of the chr overlay body — close the overlay segment. */
#pragma code-name (pop)
#endif

/* ===== XLC dispatchers: per-build placement =====
 * SWIFTAUX ports the *user-facing* XLC builtins below
 * — Int(_ s:) + the three array methods — to per-body copy-down overlays
 * (XLCINT / XLCRML / XLCRMA / XLCCON), the same way asc/chr/call_builtin
 * got their overlays in slice 2. The relocated *hot opcode* bodies
 * (str_concat / str_interp / new_array / arr_len) are NOT overlays on
 * aux: they stay inline in MAIN (vm.c, the lite path) so the common
 * string/array ops don't pay a per-call AUXMOVE, so they're #if'd out
 * here. SWIFTSAT keeps the whole set in the file-wide "XLC" segment
 * (pushed above, popped at end-of-file); host builds them as normal
 * CODE. Slice 3 step 1 = these four; the platform/GR builtins below
 * follow in step 2 (grouped). */

/* `Int(_ s:)` — ported to its own XLCINT aux overlay (slice 3). On
 * SWIFTSAT it rides the file-wide XLC push; host = normal CODE. */
#if defined(__CC65__) && defined(WITH_SWIFTAUX)
#pragma code-name (push, "XLCINT")
#endif

/* `Int(_ s: String) -> Int?` worker — parse a decimal string into an
 * int16. Accepts an optional leading '+'/'-' then one or more digits;
 * rejects empty, sign-only, non-digit, and out-of-range strings. On
 * success writes the value to *out and returns 1; on any failure
 * returns 0 (the dispatcher turns that into T_OPT_NIL). Pure +
 * separately unit-testable; uses str_bytes (MAIN) for the pool-vs-heap
 * payload split. The overflow guard compares against the per-sign
 * limit (32768 for negative, 32767 for positive) before each multiply
 * so accumulation never wraps. */
uint8_t xlc_str_to_int(uint16_t payload, int16_t *out) {
  const unsigned char *data;
  uint16_t len;
  uint16_t i;
  uint16_t acc;
  uint16_t limit;
  uint8_t neg;
  uint8_t any;
  unsigned char c;

  str_bytes(payload, &data, &len);
  if (data == (const unsigned char *)0 || len == 0) return 0;
  i = 0;
  neg = 0;
  if (data[0] == '-' || data[0] == '+') {
    neg = (data[0] == '-');
    i = 1;
  }
  limit = neg ? 32768u : 32767u;
  acc = 0;
  any = 0;
  for (; i < len; ++i) {
    c = data[i];
    if (c < '0' || c > '9') return 0;
    {
      uint16_t digit = (uint16_t)(c - '0');
      /* Reject if acc*10 + digit would exceed limit, checked without
       * overflowing: acc > (limit - digit) / 10. */
      if (acc > (uint16_t)((limit - digit) / 10u)) return 0;
      acc = (uint16_t)(acc * 10u + digit);
    }
    any = 1;
  }
  if (!any) return 0;  /* a lone sign with no digits */
  *out = neg ? (int16_t)(0u - acc) : (int16_t)acc;
  return 1;
}

/* Full BUILTIN_STR_TO_INT VM case body. Pops the T_STR argument, calls
 * the worker, pushes T_INT (some) on a successful parse or T_OPT_NIL
 * (none) otherwise. Releases the consumed string ref. Returns
 * SE_BAD_OPCODE for wrong argc, SE_TYPE_MISMATCH for a non-string arg.
 * Note: unlike asc, an invalid parse is NOT an error — it's a valid
 * `nil` result, mirroring Swift's failable Int(_:). */
swiftii_err_t xlc_str_to_int_dispatch(uint8_t argc) {
  Value va;
  Value v;
  int16_t parsed;
  uint8_t ok;
  if (argc != 1) return SE_BAD_OPCODE;
  va = s_stack[vm_sp - 1];
  if (va.tag != T_STR) return SE_TYPE_MISMATCH;
  ok = xlc_str_to_int(((uint16_t)va.hi << 8) | va.lo, &parsed);
  value_release(&va);
  vm_sp = (uint8_t)(vm_sp - 1);
  if (ok) {
    v.tag = T_INT;
    v.lo = (unsigned char)((uint16_t)parsed & 0xFF);
    v.hi = (unsigned char)(((uint16_t)parsed >> 8) & 0xFF);
  } else {
    v.tag = T_OPT_NIL;
    v.lo = 0;
    v.hi = 0;
  }
  s_stack[vm_sp++] = v;
  return SE_OK;
}

#if defined(__CC65__) && defined(WITH_SWIFTAUX)
#pragma code-name (pop)                  /* end XLCINT overlay */
#endif

#if !defined(WITH_SWB) || !defined(__CC65__)
/* (str_concat + str_interp are compiled OUT on the Family B Runner —
 * its vm.c keeps the lite inline bodies, so these would be dead code;
 * ld65 links whole objects.) */

/* str_concat — ported to its own XLCSCC aux overlay (slice 3 step 2).
 * Slice 2 kept this hot string-`+` op inline in MAIN for speed, but the
 * platform-builtin parser table (step 2) needs the MAIN headroom, so it's
 * evicted like the other cold-ish bodies; the directory-park trampoline
 * copies only its ~200 B (not a 2 KB stride), so the per-`+` cost is
 * modest. SWIFTSAT keeps it in the file-wide XLC; host = normal CODE. */
#if defined(__CC65__) && defined(WITH_SWIFTAUX)
#pragma code-name (push, "XLCSCC")
#endif

/* Relocated OP_STR_CONCAT body (XLC opcode-dispatch prototype). Not a
 * user builtin — vm.c's OP_STR_CONCAT case calls this via XLC_CALL(
 * XLC_OP_STR_CONCAT, 0). Pops the two T_STR operands, concatenates
 * through a 256-byte scratch, releases both inputs, pushes the heap
 * String result. `argc` is unused (opcode, not a builtin call). Calls
 * str_bytes / make_heap_str back in MAIN (vm.h) rather than
 * duplicating them. Byte-for-byte equivalent to the inline copy kept
 * in vm.c for lite (which have no XLC). */
swiftii_err_t xlc_str_concat_dispatch(uint8_t argc) {
  Value va;
  Value vb;
  Value v;
  const unsigned char *da;
  const unsigned char *db;
  uint16_t la;
  uint16_t lb;
  unsigned char tmp[256];
  uint16_t total;
  uint16_t i;
  swiftii_err_t rc;
  (void)argc;
  if (vm_sp < 2) return SE_STACK_UNDER;
  vb = s_stack[vm_sp - 1];
  va = s_stack[vm_sp - 2];
  if (va.tag != T_STR || vb.tag != T_STR) return SE_TYPE_MISMATCH;
  str_bytes(VALUE_PAYLOAD_U16(va), &da, &la);
  str_bytes(VALUE_PAYLOAD_U16(vb), &db, &lb);
  total = (uint16_t)(la + lb);
  if (total > sizeof(tmp)) return SE_OOM;
  for (i = 0; i < la; ++i) tmp[i] = da[i];
  for (i = 0; i < lb; ++i) tmp[la + i] = db[i];
  value_release(&vb);
  value_release(&va);
  vm_sp = (uint8_t)(vm_sp - 2);
  rc = make_heap_str(tmp, total, &v);
  if (rc != SE_OK) return rc;
  if (vm_sp >= VM_STACK_SLOTS) return SE_STACK_OVER;
  s_stack[vm_sp++] = v;
  return SE_OK;
}

#if defined(__CC65__) && defined(WITH_SWIFTAUX)
#pragma code-name (pop)                  /* end XLCSCC overlay */
#endif

/* str_interp (cold) — ported to its own XLCSIP aux overlay (slice 3
 * step 1) to reclaim MAIN headroom for the Int/array recognizers; on
 * aux vm.c routes OP_STR_INTERP_I through the copy-down trampoline
 * (slot 3 = XLC_OP_STR_INTERP - BUILTIN_XLC_FIRST). SWIFTSAT keeps it in
 * the file-wide XLC; host = normal CODE. */
#if defined(__CC65__) && defined(WITH_SWIFTAUX)
#pragma code-name (push, "XLCSIP")
#endif

/* Relocated OP_STR_INTERP_I body (XLC opcode dispatch). Polymorphic
 * int/bool/nil → heap String for interpolation / String(n); T_STR is
 * a pass-through (TOS left alone). Reached via XLC_CALL(
 * XLC_OP_STR_INTERP, 0). Calls fmt_i16 / make_heap_str back in MAIN.
 * The "true"/"false"/"nil" literals live in MAIN RODATA and are read
 * fine from XLC (MAIN stays visible while bank 1 is paged in). Kept
 * in sync with the inline copy in vm.c for lite. */
swiftii_err_t xlc_str_interp_dispatch(uint8_t argc) {
  Value va;
  Value v;
  unsigned char tmp[7];
  const unsigned char *text;
  uint16_t n;
  swiftii_err_t rc;
  (void)argc;
  if (vm_sp == 0) return SE_STACK_UNDER;
  va = s_stack[vm_sp - 1];
  switch (va.tag) {
    case T_INT:
      n = fmt_i16(VALUE_PAYLOAD_I16(va), tmp);
      text = tmp;
      break;
    case T_BOOL:
      if (va.lo) { text = (const unsigned char *)"true";  n = 4; }
      else       { text = (const unsigned char *)"false"; n = 5; }
      break;
    case T_NIL:
    case T_OPT_NIL:
      text = (const unsigned char *)"nil";
      n = 3;
      break;
    case T_STR:
      return SE_OK;  /* pass through */
    default:
      return SE_TYPE_MISMATCH;
  }
  vm_sp = (uint8_t)(vm_sp - 1);
  rc = make_heap_str(text, n, &v);
  if (rc != SE_OK) return rc;
  if (vm_sp >= VM_STACK_SLOTS) return SE_STACK_OVER;
  s_stack[vm_sp++] = v;
  return SE_OK;
}

#if defined(__CC65__) && defined(WITH_SWIFTAUX)
#pragma code-name (pop)                  /* end XLCSIP overlay */
#endif
#endif /* !(Family B Runner) — str_concat + str_interp */

/* `arr.removeLast()` — ported to its own XLCRML aux overlay (slice 3). */
#if defined(__CC65__) && defined(WITH_SWIFTAUX)
#pragma code-name (push, "XLCRML")
#endif

/* `arr.removeLast() -> element` (extras array methods).
 * Removes + returns the last element; the array is mutated in place
 * (count - 1, same heap offset) so the owning variable needs no
 * write-back, unlike `.append`. The element's refcount transfers from
 * the vacated slot to the VM stack: array_get copies the Value without
 * retaining and array_truncate drops the slot from the live range
 * without releasing, so the net ownership is exactly right with no
 * retain/release on the element. We release only the consumed array
 * container ref. SE_RUNTIME on an empty array. Stack ( arr -- elem ). */
swiftii_err_t xlc_arr_remove_last_dispatch(uint8_t argc) {
  Value va;
  Value v;
  heap_off_t arr;
  uint16_t count;
  if (argc != 1) return SE_BAD_OPCODE;
  va = s_stack[vm_sp - 1];
  if (va.tag != T_ARR) return SE_TYPE_MISMATCH;
  arr = (heap_off_t)VALUE_PAYLOAD_U16(va);
  count = array_count(arr);
  if (count == 0) return SE_RUNTIME;
  if (array_get(arr, (uint16_t)(count - 1), &v) != SE_OK) return SE_RUNTIME;
  array_truncate(arr, (uint16_t)(count - 1));
  value_release(&va);
  s_stack[vm_sp - 1] = v;
  return SE_OK;
}

#if defined(__CC65__) && defined(WITH_SWIFTAUX)
#pragma code-name (pop)                  /* end XLCRML overlay */
#endif

/* `arr.removeAll()` — ported to its own XLCRMA aux overlay (slice 3). */
#if defined(__CC65__) && defined(WITH_SWIFTAUX)
#pragma code-name (push, "XLCRMA")
#endif

/* `arr.removeAll()` (extras array methods). Releases every element, resets the
 * count to 0 in place (same offset), pushes nil so the expr-statement
 * layer discards it with OP_POP. Stack ( arr -- nil ). */
swiftii_err_t xlc_arr_remove_all_dispatch(uint8_t argc) {
  Value va;
  Value v;
  heap_off_t arr;
  if (argc != 1) return SE_BAD_OPCODE;
  va = s_stack[vm_sp - 1];
  if (va.tag != T_ARR) return SE_TYPE_MISMATCH;
  arr = (heap_off_t)VALUE_PAYLOAD_U16(va);
  array_release_elements(arr);
  array_truncate(arr, 0);
  value_release(&va);
  v.tag = T_NIL; v.lo = 0; v.hi = 0;
  s_stack[vm_sp - 1] = v;
  return SE_OK;
}

#if defined(__CC65__) && defined(WITH_SWIFTAUX)
#pragma code-name (pop)                  /* end XLCRMA overlay */
#endif

/* `arr.contains(v)` — ported to its own XLCCON aux overlay (slice 3). */
#if defined(__CC65__) && defined(WITH_SWIFTAUX)
#pragma code-name (push, "XLCCON")
#endif

/* `arr.contains(v) -> Bool` (extras array methods). Linear scan under the same
 * value equality as OP_EQ (same tag + payload bytes; reference identity
 * for heap strings, by value for Int/Bool). Releases both the needle
 * and the array container. Stack ( arr v -- bool ). */
swiftii_err_t xlc_arr_contains_dispatch(uint8_t argc) {
  Value va;
  Value vb;
  Value v;
  Value e;
  heap_off_t arr;
  uint16_t count;
  uint16_t i;
  unsigned char found;
  if (argc != 2) return SE_BAD_OPCODE;
  vb = s_stack[vm_sp - 1];   /* needle */
  va = s_stack[vm_sp - 2];   /* array */
  if (va.tag != T_ARR) return SE_TYPE_MISMATCH;
  arr = (heap_off_t)VALUE_PAYLOAD_U16(va);
  count = array_count(arr);
  found = 0;
  for (i = 0; i < count; ++i) {
    if (array_get(arr, i, &e) != SE_OK) break;
    if (e.tag == vb.tag && e.lo == vb.lo && e.hi == vb.hi) {
      found = 1;
      break;
    }
  }
  value_release(&vb);
  value_release(&va);
  vm_sp = (uint8_t)(vm_sp - 2);
  v.tag = T_BOOL;
  v.lo = found;
  v.hi = 0;
  s_stack[vm_sp++] = v;
  return SE_OK;
}

#if defined(__CC65__) && defined(WITH_SWIFTAUX)
#pragma code-name (pop)                  /* end XLCCON overlay */
#endif

#if !defined(WITH_SWB) || !defined(__CC65__)
/* (new_array + arr_len are compiled OUT on the Family B Runner — its
 * vm.c keeps the lite inline bodies.) */

/* new_array — ported to its own XLCNAR aux overlay (slice 3 step 2),
 * evicted from inline-MAIN to fund the platform-builtin parser table.
 * SWIFTSAT keeps it in the file-wide XLC; host = normal CODE. */
#if defined(__CC65__) && defined(WITH_SWIFTAUX)
#pragma code-name (push, "XLCNAR")
#endif

/* Relocated OP_NEW_ARRAY body (XLC opcode dispatch, not a user
 * builtin). `argc` carries the literal element count `n` (the operand
 * vm.c read from the bytecode and forwarded as the dispatch arg, since
 * the trampoline already transports one byte). Pops n values, allocates
 * an array, moves them into the slots (ownership transfers), pushes the
 * T_ARR. Reached via XLC_CALL(XLC_OP_NEW_ARRAY, n) from vm.c. Kept in
 * sync with the inline copy in vm.c for lite. */
swiftii_err_t xlc_new_array_dispatch(uint8_t argc) {
  unsigned char n;
  heap_off_t arr;
  Value v;
  n = argc;
  if (vm_sp < n) return SE_STACK_UNDER;
  arr = array_new((uint16_t)n);
  if (arr == HEAP_NULL) return SE_OOM;
  array_init_from_stack(arr, &s_stack[vm_sp - n], n);
  vm_sp = (uint8_t)(vm_sp - n);
  if (vm_sp >= VM_STACK_SLOTS) return SE_STACK_OVER;
  v.tag = T_ARR;
  v.lo = (unsigned char)(arr & 0xFF);
  v.hi = (unsigned char)((arr >> 8) & 0xFF);
  s_stack[vm_sp++] = v;
  return SE_OK;
}

#if defined(__CC65__) && defined(WITH_SWIFTAUX)
#pragma code-name (pop)                  /* end XLCNAR overlay */
#pragma code-name (push, "XLCALN")       /* arr_len overlay        */
#endif

/* Relocated OP_ARR_LEN body (`.count`, XLC opcode dispatch). Reads the
 * T_ARR or T_STR on TOS, releases its ref, replaces it with the T_INT
 * element/character count. `argc` unused. Reached via
 * XLC_CALL(XLC_OP_ARR_LEN, 0). Kept in sync with the inline copy in
 * vm.c for lite. */
swiftii_err_t xlc_arr_len_dispatch(uint8_t argc) {
  Value va;
  uint16_t cnt;
  (void)argc;
  if (vm_sp == 0) return SE_STACK_UNDER;
  va = s_stack[vm_sp - 1];
  if (va.tag == T_ARR) {
    cnt = array_count((heap_off_t)VALUE_PAYLOAD_U16(va));
  } else if (va.tag == T_STR) {
    /* Reinterpret the two payload bytes as a word (cc65 -Or miscompiles
     * the `lo | hi<<8` arithmetic here — see the lite copy in vm.c). */
    union { unsigned char b[2]; uint16_t w; } pl;
    const unsigned char *data;
    pl.b[0] = va.lo;
    pl.b[1] = va.hi;
    str_bytes(pl.w, &data, &cnt);
  } else {
    return SE_TYPE_MISMATCH;
  }
  value_release(&va);
  s_stack[vm_sp - 1].tag = T_INT;
  s_stack[vm_sp - 1].lo = (unsigned char)(cnt & 0xFF);
  s_stack[vm_sp - 1].hi = (unsigned char)((cnt >> 8) & 0xFF);
  return SE_OK;
}

#if defined(__CC65__) && defined(WITH_SWIFTAUX)
#pragma code-name (pop)                  /* end XLCALN overlay */
#endif
#endif /* !(Family B Runner) — new_array + arr_len */

#if !defined(WITH_SWB) || !defined(__CC65__)
/* (the relocated core OP_CALL_BUILTIN dispatcher is compiled OUT on the
 * Family B Runner — its vm.c keeps the lite inline print/readLine/min/
 * max chain.) */

/* call_builtin is the ONE cold runtime body SWIFTAUX evicts from MAIN
 *: its own XLCCALL aux overlay carries the fat
 * print/readLine/min/max bodies out of MAIN, funding asc/chr's parser
 * recognition. The hot ops (STR_CONCAT/INTERP/NEW_ARRAY/ARR_LEN) stay
 * inline in MAIN on aux (vm.c), so only this cold one pays a per-call
 * copy-down. On SWIFTSAT it stays in the file-wide "XLC" segment; on
 * host, normal CODE. */
#if defined(__CC65__) && defined(WITH_SWIFTAUX)
#pragma code-name (push, "XLCCALL")
#endif

/* Relocated OP_CALL_BUILTIN core bodies — print / print(_:terminator:)
 * / readLine / min / max. The builtin id arrives via xlc_builtin_id
 * (vm.c sets it before the call), argc via the dispatch-arg slot. Calls
 * builtins_print_* / platform_read_line / make_heap_str back in MAIN.
 * Byte-equivalent to the inline copy vm.c keeps for lite.
 * Unknown ids (< BUILTIN_XLC_FIRST but none of the below) are
 * SE_BAD_OPCODE — ids >= BUILTIN_XLC_FIRST never reach here (vm.c routes
 * them to their own table slots). */
swiftii_err_t xlc_call_builtin_dispatch(uint8_t argc) {
  uint8_t builtin_id;
  Value va;
  Value vb;
  Value v;
  swiftii_err_t rc;
  builtin_id = xlc_builtin_id;
  if (builtin_id == BUILTIN_PRINT) {
    unsigned char i;
    for (i = 0; i < argc; ++i) {
      builtins_print_value(&s_stack[vm_sp - argc + i]);
    }
    builtins_print_newline();
    for (i = 0; i < argc; ++i) {
      value_release(&s_stack[vm_sp - argc + i]);
    }
    vm_sp = (uint8_t)(vm_sp - argc);
    if (vm_sp >= VM_STACK_SLOTS) return SE_STACK_OVER;
    v.tag = T_NIL; v.lo = 0; v.hi = 0;
    s_stack[vm_sp++] = v;
  } else if (builtin_id == BUILTIN_PRINT_T) {
    unsigned char i;
    Value *term;
    if (argc == 0) return SE_BAD_OPCODE;
    term = &s_stack[vm_sp - 1];
    if (term->tag != T_STR) return SE_TYPE_MISMATCH;
    for (i = 0; i < (unsigned char)(argc - 1); ++i) {
      builtins_print_value(&s_stack[vm_sp - argc + i]);
    }
    builtins_print_value(term);
    for (i = 0; i < argc; ++i) {
      value_release(&s_stack[vm_sp - argc + i]);
    }
    vm_sp = (uint8_t)(vm_sp - argc);
    if (vm_sp >= VM_STACK_SLOTS) return SE_STACK_OVER;
    v.tag = T_NIL; v.lo = 0; v.hi = 0;
    s_stack[vm_sp++] = v;
  } else if (builtin_id == BUILTIN_READLINE) {
    char linebuf[256];
    int16_t n;
    if (argc != 0) return SE_BAD_OPCODE;
#if defined(__CC65__) && defined(WITH_SWIFTSAT)
    g_read_line_in_xlc = 1;
#endif
    n = platform_read_line(linebuf, (uint16_t)sizeof(linebuf));
#if defined(__CC65__) && defined(WITH_SWIFTSAT)
    g_read_line_in_xlc = 0;
#endif
    if (vm_sp >= VM_STACK_SLOTS) return SE_STACK_OVER;
    if (n <= 0) {
      v.tag = T_OPT_NIL; v.lo = 0; v.hi = 0;
      s_stack[vm_sp++] = v;
    } else {
      uint16_t real_len;
      real_len = (uint16_t)n;
      if (real_len > 0 && linebuf[real_len - 1] == '\n') {
        --real_len;
      }
      rc = make_heap_str((const unsigned char *)linebuf, real_len, &v);
      if (rc != SE_OK) return rc;
      s_stack[vm_sp++] = v;
    }
  } else if (builtin_id == BUILTIN_MIN || builtin_id == BUILTIN_MAX) {
    int16_t a;
    int16_t b;
    int16_t r;
    if (argc != 2) return SE_BAD_OPCODE;
    vb = s_stack[vm_sp - 1];
    va = s_stack[vm_sp - 2];
    if (va.tag != T_INT || vb.tag != T_INT) return SE_TYPE_MISMATCH;
    a = (int16_t)VALUE_PAYLOAD_I16(va);
    b = (int16_t)VALUE_PAYLOAD_I16(vb);
    r = (builtin_id == BUILTIN_MIN) ? (a < b ? a : b) : (a > b ? a : b);
    vm_sp = (uint8_t)(vm_sp - 2);
    v.tag = T_INT;
    v.lo = (unsigned char)((uint16_t)r & 0xFF);
    v.hi = (unsigned char)(((uint16_t)r >> 8) & 0xFF);
    s_stack[vm_sp++] = v;
#if !defined(__CC65__)
  } else if (builtin_id == BUILTIN_ABS || builtin_id == BUILTIN_SGN) {
    /* abs / sgn — Family B, HOST path only (gated !__CC65__ so SWIFTSAT/
     * SWIFTAUX REPLs, which compile this dispatcher into their XLC bank,
     * stay byte-identical — they never emit abs/sgn anyway). The Family B
     * Runner uses the byte-identical inline copy in vm.c. One Int arg,
     * replaced in place. abs negates a negative; sgn yields -1/0/1. */
    int16_t a;
    int16_t r;
    if (argc != 1) return SE_BAD_OPCODE;
    va = s_stack[vm_sp - 1];
    if (va.tag != T_INT) return SE_TYPE_MISMATCH;
    a = (int16_t)VALUE_PAYLOAD_I16(va);
    if (builtin_id == BUILTIN_ABS)
      r = (int16_t)(a < 0 ? -a : a);
    else
      r = (int16_t)((a > 0) - (a < 0));
    s_stack[vm_sp - 1].lo = (unsigned char)((uint16_t)r & 0xFF);
    s_stack[vm_sp - 1].hi = (unsigned char)(((uint16_t)r >> 8) & 0xFF);
#endif
#if SWIFTII_RANDOM
  } else if (builtin_id == BUILTIN_RANDOM_LT ||
             builtin_id == BUILTIN_RANDOM_LE) {
    /* random(in:) — host path (Family B uses the vm.c inline copy). Shares
     * the LCG worker so there's one RNG. */
    swiftii_err_t rrc;
    if (argc != 2) return SE_BAD_OPCODE;
    vb = s_stack[vm_sp - 1];
    va = s_stack[vm_sp - 2];
    if (va.tag != T_INT || vb.tag != T_INT) return SE_TYPE_MISMATCH;
    rrc = builtin_random_in((int16_t)VALUE_PAYLOAD_I16(va),
                            (int16_t)VALUE_PAYLOAD_I16(vb),
                            (unsigned char)(builtin_id == BUILTIN_RANDOM_LE),
                            &v);
    if (rrc != SE_OK) return rrc;
    vm_sp = (uint8_t)(vm_sp - 2);
    s_stack[vm_sp++] = v;
#endif
#if !defined(__CC65__)
  } else if (builtin_id == BUILTIN_WAIT) {
    /* wait(_ ms: Int) -> Void — busy-wait ~ms milliseconds, push nil.
     * The real delay lives in platform_wait_ms (ROM WAIT $FCA8 loop on cc65,
     * no-op on host). This copy is the HOST path only; on cc65 wait() ships
     * solely on the Family B Runner, which keeps a byte-identical inline copy
     * in vm.c (no REPL ships wait()). ( ms -- nil ). */
    if (argc != 1) return SE_BAD_OPCODE;
    va = s_stack[vm_sp - 1];
    if (va.tag != T_INT) return SE_TYPE_MISMATCH;
    platform_wait_ms(VALUE_PAYLOAD_U16(va));
    s_stack[vm_sp - 1].tag = T_NIL;
    s_stack[vm_sp - 1].lo = 0;
    s_stack[vm_sp - 1].hi = 0;
  } else if (builtin_id == BUILTIN_TONE) {
    /* tone(_ halfPeriod: Int, _ cycles: Int) -> Void — HOST path (no-op
     * platform_tone keeps test output deterministic); on cc65 tone() ships
     * solely on the Family B Runner, which keeps a byte-identical inline copy
     * in vm.c. ( halfPeriod cycles -- nil ). */
    if (argc != 2) return SE_BAD_OPCODE;
    vb = s_stack[vm_sp - 1];
    va = s_stack[vm_sp - 2];
    if (va.tag != T_INT || vb.tag != T_INT) return SE_TYPE_MISMATCH;
    platform_tone(VALUE_PAYLOAD_U16(va), VALUE_PAYLOAD_U16(vb));
    vm_sp = (uint8_t)(vm_sp - 2);
    v.tag = T_NIL; v.lo = 0; v.hi = 0;
    s_stack[vm_sp++] = v;
  } else if (builtin_id == BUILTIN_HAS_PREFIX ||
             builtin_id == BUILTIN_HAS_SUFFIX) {
    /* hasPrefix(t)/hasSuffix(t) -> Bool — HOST path; on cc65 these ship
     * solely on the Family B Runner, which keeps a byte-identical inline
     * copy in vm.c calling the same xlc_has_affix worker. Stack is
     * ( receiver needle -- bool ); both T_STR, both released. */
    if (argc != 2) return SE_BAD_OPCODE;
    vb = s_stack[vm_sp - 1];
    va = s_stack[vm_sp - 2];
    if (va.tag != T_STR || vb.tag != T_STR) return SE_TYPE_MISMATCH;
    v.tag = T_BOOL;
    v.lo = (unsigned char)xlc_has_affix(VALUE_PAYLOAD_U16(va),
                                        VALUE_PAYLOAD_U16(vb),
                                        (unsigned char)(builtin_id ==
                                                        BUILTIN_HAS_SUFFIX));
    v.hi = 0;
    value_release(&vb);
    value_release(&va);
    vm_sp = (uint8_t)(vm_sp - 2);
    s_stack[vm_sp++] = v;
#endif /* !__CC65__ — wait()/tone()/has* host path (cc65 = Family B Runner inline) */
  } else {
    return SE_BAD_OPCODE;
  }
  return SE_OK;
}
#if defined(__CC65__) && defined(WITH_SWIFTAUX)
#pragma code-name (pop)                  /* end XLCCALL overlay */
#endif
#endif /* !(Family B Runner) — relocated core call_builtin */

/* ===== platform builtins — per-build placement =====
 * SWIFTAUX ports these too, but unlike the
 * step-1 builtins it GROUPS them into two copy-down overlays reached via
 * an internal id-switch (mirrors xlc_call_builtin_dispatch): there are 13
 * of them and their ids ($18-$24) map to slots 11-23, which would overflow
 * the fixed-stride aux park — so they instead share two free gap slots (8
 * = XLC_OP_NEW_ARRAY, 9 = XLC_OP_ARR_LEN; both inline-in-MAIN on aux, so
 * their park slots are unused). A compile-time id→slot table in aux_xlc.s
 * routes $18-$24 to those two physical slots; the group dispatchers
 * (xlc_pmem_dispatch / xlc_pgr_dispatch) switch on xlc_builtin_id. Group A
 * (XLCPMEM) = the helper-free cursor/memory builtins; Group B (XLCPGR) =
 * the GR-page builtins, which must co-locate with the shared static
 * helpers gr_set_block/gr_get_block/gr_enter. SWIFTSAT keeps each at its
 * own file-wide-XLC dispatcher (xlc_table.s slot); host = normal CODE. */
#if defined(__CC65__) && defined(WITH_SWIFTAUX)
#pragma code-name (push, "XLCPMEM")
#endif

/* `home()` — clear the text screen and home the cursor. Routes to the
 * existing platform_clear_screen() (cc65 clrscr() on target, ANSI
 * escape on host). Stack ( -- nil ): the nil result is discarded by the
 * expr-statement OP_POP. argc must be 0. */
swiftii_err_t xlc_home_dispatch(uint8_t argc) {
  Value v;
  if (argc != 0) return SE_BAD_OPCODE;
  platform_clear_screen();
  if (vm_sp >= VM_STACK_SLOTS) return SE_STACK_OVER;
  v.tag = T_NIL; v.lo = 0; v.hi = 0;
  s_stack[vm_sp++] = v;
  return SE_OK;
}

/* `peek(_ addr: Int) -> Int` — read one byte of main RAM (0..255). The
 * cc65 path dereferences `addr` directly; main RAM is visible from XLC
 * regardless of the Saturn bank-select state. The host path returns 0
 * (no raw-memory access — keeps the test suite deterministic and safe).
 * Stack ( addr -- byte ). argc must be 1. */
swiftii_err_t xlc_peek_dispatch(uint8_t argc) {
  Value va;
  Value v;
  uint16_t addr;
  if (argc != 1) return SE_BAD_OPCODE;
  va = s_stack[vm_sp - 1];
  if (va.tag != T_INT) return SE_TYPE_MISMATCH;
  addr = VALUE_PAYLOAD_U16(va);
  v.tag = T_INT;
#ifdef __CC65__
  v.lo = *((volatile unsigned char *)addr);
#else
  (void)addr;
  v.lo = 0;
#endif
  v.hi = 0;
  s_stack[vm_sp - 1] = v;
  return SE_OK;
}

/* `poke(_ addr: Int, _ value: Int)` — write the low byte of `value` to
 * main RAM at `addr`. cc65 stores directly; host is a no-op. A poke to
 * a soft switch such as $C030 (49200) toggles the speaker — the free
 * "click" primitive. Stack ( addr value -- nil ). argc must be 2. */
swiftii_err_t xlc_poke_dispatch(uint8_t argc) {
  Value va;
  Value vv;
  Value v;
  uint16_t addr;
  if (argc != 2) return SE_BAD_OPCODE;
  vv = s_stack[vm_sp - 1];
  va = s_stack[vm_sp - 2];
  if (va.tag != T_INT || vv.tag != T_INT) return SE_TYPE_MISMATCH;
  addr = VALUE_PAYLOAD_U16(va);
#ifdef __CC65__
  *((volatile unsigned char *)addr) = vv.lo;
#else
  (void)addr;
#endif
  vm_sp = (uint8_t)(vm_sp - 2);
  v.tag = T_NIL; v.lo = 0; v.hi = 0;
  s_stack[vm_sp++] = v;
  return SE_OK;
}

/* `htab(_ col: Int)` — move the text cursor to 1-based column 1..40,
 * preserving the row. Out-of-range is SE_RUNTIME (matches the
 * bounds-checked array philosophy rather than silently clamping).
 * platform_htab does the cc65 gotoxy / host no-op. Stack ( col -- nil ). */
swiftii_err_t xlc_htab_dispatch(uint8_t argc) {
  Value va;
  int16_t col;
  if (argc != 1) return SE_BAD_OPCODE;
  va = s_stack[vm_sp - 1];
  if (va.tag != T_INT) return SE_TYPE_MISMATCH;
  col = (int16_t)VALUE_PAYLOAD_I16(va);
#if defined(WITH_80COL) && \
    (defined(WITH_SWIFTSAT) || defined(WITH_SWIFTAUX) || defined(WITH_SWB))
  /* 1..80 in 80-col mode, 1..40 otherwise. The //e firmware path and II+
   * Videx path both provide platform_text_width() when WITH_80COL is present;
   * builds without an 80-col arm keep the literal 1..40. */
  if (col < 1 || col > (int16_t)platform_text_width()) return SE_RUNTIME;
#else
  if (col < 1 || col > 40) return SE_RUNTIME;
#endif
  platform_htab((uint8_t)col);
  s_stack[vm_sp - 1].tag = T_NIL;
  s_stack[vm_sp - 1].lo = 0;
  s_stack[vm_sp - 1].hi = 0;
  return SE_OK;
}

/* `vtab(_ row: Int)` — move the text cursor to 1-based row 1..24,
 * preserving the column. SE_RUNTIME out of range. Stack ( row -- nil ). */
swiftii_err_t xlc_vtab_dispatch(uint8_t argc) {
  Value va;
  int16_t row;
  if (argc != 1) return SE_BAD_OPCODE;
  va = s_stack[vm_sp - 1];
  if (va.tag != T_INT) return SE_TYPE_MISMATCH;
  row = (int16_t)VALUE_PAYLOAD_I16(va);
  if (row < 1 || row > 24) return SE_RUNTIME;
  platform_vtab((uint8_t)row);
  s_stack[vm_sp - 1].tag = T_NIL;
  s_stack[vm_sp - 1].lo = 0;
  s_stack[vm_sp - 1].hi = 0;
  return SE_OK;
}

#if defined(WITH_SWIFTAUX)
/* Group A entry (aux-only): the boot-launcher loader parks this whole overlay
 * at slot 8; aux_xlc_call copies it down and JSRs the aux_table.s stub →
 * here, which fans out to the right cursor/memory dispatcher by the id
 * vm.c stashed in xlc_builtin_id. SWIFTSAT/host reach each dispatcher
 * directly (table slot / switch) and don't compile this. */
swiftii_err_t xlc_pmem_dispatch(uint8_t argc) {
  switch (xlc_builtin_id) {
    case BUILTIN_HOME: return xlc_home_dispatch(argc);
    case BUILTIN_PEEK: return xlc_peek_dispatch(argc);
    case BUILTIN_POKE: return xlc_poke_dispatch(argc);
    case BUILTIN_HTAB: return xlc_htab_dispatch(argc);
    case BUILTIN_VTAB: return xlc_vtab_dispatch(argc);
    default:           return SE_BAD_OPCODE;
  }
}
#endif

#if defined(__CC65__) && defined(WITH_SWIFTAUX)
#pragma code-name (pop)                  /* end XLCPMEM (Group A) overlay */
#pragma code-name (push, "XLCPGR")       /* Group B: GR-page builtins     */
#endif

/* Low-res graphics. The hardware work (soft
 * switches, GR-page writes) is cc65-only and lives HERE in XLC, not in
 * screen.c (MAIN), where it overflowed the tight SWIFTSAT MAIN budget.
 * Host builds run only the arg validation (range checks) — the drawing
 * is a no-op, exactly like peek/poke. Mixed GR matches Applesoft `GR`:
 * 40x40 colour blocks in text rows 0..19 + a 4-line text window
 * (20..23). The GR page IS text page 1 ($0400-$07FF) reinterpreted:
 * each byte holds two stacked blocks, low nibble = upper block (even
 * y), high nibble = lower block (odd y); GR pixel row y maps to text
 * row y>>1. Conio cursor/clear reuse the existing MAIN platform helpers
 * (platform_clear_screen/htab/vtab) so no new MAIN code is added. */
static unsigned char s_gr_color;
/* Active GR mode: 0 = mixed (40x40, plot y 0..39), 1 = full (40x48,
 * plot y 0..47). Set by gr()/grFull(), read by plot's range check.
 * Tracked on host too (host draws nothing but still validates coords). */
static unsigned char s_gr_full;

#ifdef __CC65__
/* GR/text-page row base: $0400 + (r&7)*$80 + (r>>3)*40. Mirrors the
 * private row_base() in screen.c (replicated rather than exported for
 * one XLC caller — same call as asc/chr replicating str helpers). */
static unsigned char *gr_row_base(unsigned char r) {
  return (unsigned char *)(uint16_t)
    (0x0400u + ((uint16_t)(r & 7) << 7) + ((uint16_t)(r >> 3) * 40u));
}

/* Write one GR block at (x, y) in the current colour. y's low bit picks
 * the nibble (even = upper/low nibble, odd = lower/high nibble); text
 * row = y>>1. Shared by plot / hlin / vlin. */
static void gr_set_block(unsigned char x, unsigned char y) {
  unsigned char *cell = gr_row_base((unsigned char)(y >> 1)) + x;
  if (y & 1) {
    *cell = (unsigned char)((*cell & 0x0F) | (s_gr_color << 4));
  } else {
    *cell = (unsigned char)((*cell & 0xF0) | s_gr_color);
  }
}

/* Read the GR colour (0..15) at (x, y). Inverse of gr_set_block. */
static unsigned char gr_get_block(unsigned char x, unsigned char y) {
  unsigned char cell = *(gr_row_base((unsigned char)(y >> 1)) + x);
  if (y & 1) {
    return (unsigned char)((cell >> 4) & 0x0F);
  }
  return (unsigned char)(cell & 0x0F);
}

/* Enter lo-res graphics. `full` selects full-screen 40x48 (MIXCLR,
 * whole page is graphics) vs mixed 40x40 (MIXSET, 4-line text window).
 * Clears the graphics area to black; in mixed mode also blanks the text
 * window and drops the cursor into it so print() lands there. */
static void gr_enter(unsigned char full) {
  unsigned char r;
  unsigned char j;
  unsigned char *base;
  unsigned char gfx_rows;

  /* Touch the display soft switches with a STORE, not a discarded
   * read: cc65's optimizer elides `(void)(*(volatile T*)addr)` even
   * with `volatile` (verified — the reads vanished from the object,
   * so gr() never switched modes and the cleared page showed as
   * inverse-@ text). A volatile store is never elided, and any access
   * toggles these switches — this is exactly Applesoft's POKE 49232,0
   * idiom. */
  *(volatile unsigned char *)0xC050u = 0;  /* TXTCLR graphics      */
  if (full) *(volatile unsigned char *)0xC052u = 0;  /* MIXCLR full */
  else      *(volatile unsigned char *)0xC053u = 0;  /* MIXSET mixed*/
  *(volatile unsigned char *)0xC054u = 0;  /* LOWSCR page 1        */
  *(volatile unsigned char *)0xC056u = 0;  /* LORES lo-res         */

  gfx_rows = full ? 24 : 20;             /* 48 vs 40 pixel rows         */
  for (r = 0; r < gfx_rows; ++r) {       /* graphics area -> black      */
    base = gr_row_base(r);
    for (j = 0; j < 40; ++j) base[j] = 0x00;
  }
  if (!full) {
    for (r = 20; r < 24; ++r) {          /* text window -> spaces       */
      base = gr_row_base(r);
      for (j = 0; j < 40; ++j) base[j] = 0xA0;
    }
    platform_htab(1);    /* drop the cursor into the text window         */
    platform_vtab(21);
  }
#if defined(WITH_GR_TEXTWIN)
  /* Confine print() scrolling to the 4-line text window (rows 20-23) in
   * mixed mode so it doesn't drag the GR area up; full mode scrolls the
   * whole screen as before. */
  g_gr_scroll_top = full ? 0 : 20;
#endif
}
#endif

/* Shared gr()/grFull() body: enter the mode, reset colour, remember the
 * mode for plot's range check, push nil. Stack ( -- nil ). */
static swiftii_err_t gr_dispatch_common(uint8_t argc, unsigned char full) {
  Value v;
  if (argc != 0) return SE_BAD_OPCODE;
#ifdef __CC65__
  gr_enter(full);
#endif
  s_gr_color = 0;
  s_gr_full = full;
  if (vm_sp >= VM_STACK_SLOTS) return SE_STACK_OVER;
  v.tag = T_NIL; v.lo = 0; v.hi = 0;
  s_stack[vm_sp++] = v;
  return SE_OK;
}

/* `gr()` — mixed low-res graphics (40x40 + 4-line text window). */
swiftii_err_t xlc_gr_dispatch(uint8_t argc) {
  return gr_dispatch_common(argc, 0);
}

/* `grFull()` — full-screen low-res graphics (40x48, no text window). */
swiftii_err_t xlc_gr_full_dispatch(uint8_t argc) {
  return gr_dispatch_common(argc, 1);
}

/* `text()` — return to 40-column text mode and clear. On //e builds this
 * also drops out of 80-column mode (the symmetric partner of text80()).
 * Stack ( -- nil ). */
swiftii_err_t xlc_text_dispatch(uint8_t argc) {
  Value v;
  if (argc != 0) return SE_BAD_OPCODE;
#ifdef __CC65__
  *(volatile unsigned char *)0xC051u = 0;       /* TXTSET full text   */
#if defined(WITH_GR_TEXTWIN)
  g_gr_scroll_top = 0;                          /* full-screen scroll */
#endif
#if defined(WITH_80COL)
  /* Drop out of 80-col mode and clear: //e turns off 80COL/80STORE +
   * resets WNDWDTH, then clrscr the 40-col page. (text80() is the
   * symmetric re-entry.) On SWIFTSAT (no WITH_80COL) this branch is
   * compiled out and text() just clears. */
  platform_text40();
#else
  platform_clear_screen();                      /* clrscr + home      */
#endif
#endif
  if (vm_sp >= VM_STACK_SLOTS) return SE_STACK_OVER;
  v.tag = T_NIL; v.lo = 0; v.hi = 0;
  s_stack[vm_sp++] = v;
  return SE_OK;
}

#if defined(WITH_SWIFTAUX) || defined(WITH_SWB) || defined(WITH_SWIFTSAT) || \
    !defined(__CC65__)
/* `text80()` — switch the console to 80 columns. The real mode switch is
 * platform_text80() (screen.c, MAIN), a no-op unless an 80-col card is
 * detected at runtime: the //e built-in card (WITH_IIE binaries) or the II+
 * Videx Videoterm (SWIFTSAT + II+ Runner). On host — and on any binary built
 * without a WITH_80COL path — it's a push-nil no-op, which is what lets the
 * one shared Family B Compiler recognise text80() for every disk.
 * Stack ( -- nil ). */
swiftii_err_t xlc_text80_dispatch(uint8_t argc) {
  Value v;
  if (argc != 0) return SE_BAD_OPCODE;
#if defined(WITH_80COL)
  platform_text80();
#endif
  if (vm_sp >= VM_STACK_SLOTS) return SE_STACK_OVER;
  v.tag = T_NIL; v.lo = 0; v.hi = 0;
  s_stack[vm_sp++] = v;
  return SE_OK;
}
#endif

/* `color(_ n: Int)` — set the current GR colour 0..15. SE_RUNTIME out
 * of range. Stack ( n -- nil ). */
swiftii_err_t xlc_color_dispatch(uint8_t argc) {
  Value va;
  int16_t n;
  if (argc != 1) return SE_BAD_OPCODE;
  va = s_stack[vm_sp - 1];
  if (va.tag != T_INT) return SE_TYPE_MISMATCH;
  n = (int16_t)VALUE_PAYLOAD_I16(va);
  if (n < 0 || n > 15) return SE_RUNTIME;
  s_gr_color = (unsigned char)n;
  s_stack[vm_sp - 1].tag = T_NIL;
  s_stack[vm_sp - 1].lo = 0;
  s_stack[vm_sp - 1].hi = 0;
  return SE_OK;
}

/* `plot(_ x: Int, _ y: Int)` — draw one block at (x, y) in the current
 * colour. x 0..39 always; y 0..39 in mixed GR, 0..47 in full-screen GR
 * (per the active mode set by gr()/grFull()). SE_RUNTIME out of range.
 * Stack ( x y -- nil ). */
swiftii_err_t xlc_plot_dispatch(uint8_t argc) {
  Value vx;
  Value vy;
  Value v;
  int16_t x;
  int16_t y;
  int16_t ymax;
  if (argc != 2) return SE_BAD_OPCODE;
  vy = s_stack[vm_sp - 1];
  vx = s_stack[vm_sp - 2];
  if (vx.tag != T_INT || vy.tag != T_INT) return SE_TYPE_MISMATCH;
  x = (int16_t)VALUE_PAYLOAD_I16(vx);
  y = (int16_t)VALUE_PAYLOAD_I16(vy);
  ymax = s_gr_full ? 47 : 39;
  if (x < 0 || x > 39 || y < 0 || y > ymax) return SE_RUNTIME;
#ifdef __CC65__
  gr_set_block((unsigned char)x, (unsigned char)y);
#endif
  vm_sp = (uint8_t)(vm_sp - 2);
  v.tag = T_NIL; v.lo = 0; v.hi = 0;
  s_stack[vm_sp++] = v;
  return SE_OK;
}

/* `hlin(_ x1: Int, _ x2: Int, at y: Int)` — horizontal run of blocks
 * from x1 to x2 (inclusive, either order) at row y, in the current
 * colour. x 0..39, y 0..mode-max. SE_RUNTIME out of range. argc 3.
 * Stack ( x1 x2 y -- nil ). */
swiftii_err_t xlc_hlin_dispatch(uint8_t argc) {
  Value v;
  int16_t x1;
  int16_t x2;
  int16_t y;
  int16_t ymax;
  if (argc != 3) return SE_BAD_OPCODE;
  if (s_stack[vm_sp - 3].tag != T_INT || s_stack[vm_sp - 2].tag != T_INT ||
      s_stack[vm_sp - 1].tag != T_INT) {
    return SE_TYPE_MISMATCH;
  }
  x1 = (int16_t)VALUE_PAYLOAD_I16(s_stack[vm_sp - 3]);
  x2 = (int16_t)VALUE_PAYLOAD_I16(s_stack[vm_sp - 2]);
  y  = (int16_t)VALUE_PAYLOAD_I16(s_stack[vm_sp - 1]);
  ymax = s_gr_full ? 47 : 39;
  if (x1 < 0 || x1 > 39 || x2 < 0 || x2 > 39 || y < 0 || y > ymax) {
    return SE_RUNTIME;
  }
  if (x1 > x2) { int16_t t = x1; x1 = x2; x2 = t; }
#ifdef __CC65__
  {
    int16_t x;
    for (x = x1; x <= x2; ++x) gr_set_block((unsigned char)x, (unsigned char)y);
  }
#endif
  vm_sp = (uint8_t)(vm_sp - 3);
  v.tag = T_NIL; v.lo = 0; v.hi = 0;
  s_stack[vm_sp++] = v;
  return SE_OK;
}

/* `vlin(_ y1: Int, _ y2: Int, at x: Int)` — vertical run of blocks from
 * y1 to y2 (inclusive, either order) at column x. y 0..mode-max,
 * x 0..39. SE_RUNTIME out of range. argc 3. Stack ( y1 y2 x -- nil ). */
swiftii_err_t xlc_vlin_dispatch(uint8_t argc) {
  Value v;
  int16_t y1;
  int16_t y2;
  int16_t x;
  int16_t ymax;
  if (argc != 3) return SE_BAD_OPCODE;
  if (s_stack[vm_sp - 3].tag != T_INT || s_stack[vm_sp - 2].tag != T_INT ||
      s_stack[vm_sp - 1].tag != T_INT) {
    return SE_TYPE_MISMATCH;
  }
  y1 = (int16_t)VALUE_PAYLOAD_I16(s_stack[vm_sp - 3]);
  y2 = (int16_t)VALUE_PAYLOAD_I16(s_stack[vm_sp - 2]);
  x  = (int16_t)VALUE_PAYLOAD_I16(s_stack[vm_sp - 1]);
  ymax = s_gr_full ? 47 : 39;
  if (y1 < 0 || y1 > ymax || y2 < 0 || y2 > ymax || x < 0 || x > 39) {
    return SE_RUNTIME;
  }
  if (y1 > y2) { int16_t t = y1; y1 = y2; y2 = t; }
#ifdef __CC65__
  {
    int16_t y;
    for (y = y1; y <= y2; ++y) gr_set_block((unsigned char)x, (unsigned char)y);
  }
#endif
  vm_sp = (uint8_t)(vm_sp - 3);
  v.tag = T_NIL; v.lo = 0; v.hi = 0;
  s_stack[vm_sp++] = v;
  return SE_OK;
}

/* `scrn(_ x: Int, _ y: Int) -> Int` — read the GR colour 0..15 at
 * (x, y). x 0..39, y 0..mode-max. SE_RUNTIME out of range. Host returns
 * 0 (no GR page). argc 2. Stack ( x y -- colour ). */
swiftii_err_t xlc_scrn_dispatch(uint8_t argc) {
  Value va;
  Value vb;
  Value v;
  int16_t x;
  int16_t y;
  int16_t ymax;
  unsigned char c;
  if (argc != 2) return SE_BAD_OPCODE;
  vb = s_stack[vm_sp - 1];   /* y */
  va = s_stack[vm_sp - 2];   /* x */
  if (va.tag != T_INT || vb.tag != T_INT) return SE_TYPE_MISMATCH;
  x = (int16_t)VALUE_PAYLOAD_I16(va);
  y = (int16_t)VALUE_PAYLOAD_I16(vb);
  ymax = s_gr_full ? 47 : 39;
  if (x < 0 || x > 39 || y < 0 || y > ymax) return SE_RUNTIME;
#ifdef __CC65__
  c = gr_get_block((unsigned char)x, (unsigned char)y);
#else
  c = 0;
#endif
  vm_sp = (uint8_t)(vm_sp - 2);
  v.tag = T_INT;
  v.lo = c;
  v.hi = 0;
  s_stack[vm_sp++] = v;
  return SE_OK;
}

#if defined(WITH_SWIFTAUX)
/* Group B entry (aux-only): parked at slot 9, fans out to the GR-page
 * dispatcher named by xlc_builtin_id. Co-located with the gr_* static
 * helpers above so its members' calls into them resolve STAGING-relative
 * after copy-down. */
swiftii_err_t xlc_pgr_dispatch(uint8_t argc) {
  switch (xlc_builtin_id) {
    case BUILTIN_GR:      return xlc_gr_dispatch(argc);
    case BUILTIN_GR_FULL: return xlc_gr_full_dispatch(argc);
    case BUILTIN_TEXT:    return xlc_text_dispatch(argc);
    case BUILTIN_COLOR:   return xlc_color_dispatch(argc);
    case BUILTIN_PLOT:    return xlc_plot_dispatch(argc);
    case BUILTIN_HLIN:    return xlc_hlin_dispatch(argc);
    case BUILTIN_VLIN:    return xlc_vlin_dispatch(argc);
    case BUILTIN_SCRN:    return xlc_scrn_dispatch(argc);
    case BUILTIN_TEXT80:  return xlc_text80_dispatch(argc);
    default:              return SE_BAD_OPCODE;
  }
}
#endif

#if defined(__CC65__) && !defined(WITH_SWB)
/* Pops XLCPGR (Group B) on aux; pops the file-wide XLC on SWIFTSAT.
 * The Family B Runner pushed nothing (normal CODE) — no pop. */
#pragma code-name (pop)
#endif

#if !defined(__CC65__) || defined(WITH_SWB)
/* Host parallel to the cc65 JMP table at XLC offset 0 (xlc_table.s).
 * vm.c's XLC_CALL macro routes through here when there's no bus to
 * switch. The Family B Runner (cc65 + WITH_SWB) uses the same switch as
 * normal CODE — but only for its real surface: the relocated-opcode +
 * core-call_builtin cases are gated out there (vm.c keeps those bodies
 * inline; the dispatchers aren't compiled). Add a case alongside any
 * new xlc_*_dispatch worker. Unknown ids fall through to SE_BAD_OPCODE,
 * mirroring the cc65 path's behaviour for ids past the table end (the
 * trampoline would JSR through an out-of-range JMP slot otherwise — we
 * trust vm.c's range check to gate that on cc65). */
swiftii_err_t xlc_call_dispatch(uint8_t id, uint8_t argc) {
  switch (id) {
    case BUILTIN_ASC:        return xlc_asc_dispatch(argc);
    case BUILTIN_CHR:        return xlc_chr_dispatch(argc);
    case BUILTIN_STR_TO_INT: return xlc_str_to_int_dispatch(argc);
#if !defined(WITH_SWB) || !defined(__CC65__)
    case XLC_OP_STR_CONCAT:  return xlc_str_concat_dispatch(argc);
    case XLC_OP_STR_INTERP:  return xlc_str_interp_dispatch(argc);
#endif
    case BUILTIN_ARR_REMOVE_LAST: return xlc_arr_remove_last_dispatch(argc);
    case BUILTIN_ARR_REMOVE_ALL:  return xlc_arr_remove_all_dispatch(argc);
    case BUILTIN_ARR_CONTAINS:    return xlc_arr_contains_dispatch(argc);
#if !defined(WITH_SWB) || !defined(__CC65__)
    case XLC_OP_NEW_ARRAY:   return xlc_new_array_dispatch(argc);
    case XLC_OP_ARR_LEN:     return xlc_arr_len_dispatch(argc);
    case XLC_OP_CALL_BUILTIN: return xlc_call_builtin_dispatch(argc);
#endif
    case BUILTIN_HOME:       return xlc_home_dispatch(argc);
    case BUILTIN_PEEK:       return xlc_peek_dispatch(argc);
    case BUILTIN_POKE:       return xlc_poke_dispatch(argc);
    case BUILTIN_HTAB:       return xlc_htab_dispatch(argc);
    case BUILTIN_VTAB:       return xlc_vtab_dispatch(argc);
    case BUILTIN_GR:         return xlc_gr_dispatch(argc);
    case BUILTIN_GR_FULL:    return xlc_gr_full_dispatch(argc);
    case BUILTIN_TEXT:       return xlc_text_dispatch(argc);
    case BUILTIN_COLOR:      return xlc_color_dispatch(argc);
    case BUILTIN_PLOT:       return xlc_plot_dispatch(argc);
    case BUILTIN_HLIN:       return xlc_hlin_dispatch(argc);
    case BUILTIN_VLIN:       return xlc_vlin_dispatch(argc);
    case BUILTIN_SCRN:       return xlc_scrn_dispatch(argc);
    case BUILTIN_TEXT80:     return xlc_text80_dispatch(argc);
    default:                 return SE_BAD_OPCODE;
  }
}
#endif

#endif /* WITH_SWIFTSAT || WITH_SWIFTAUX || WITH_SWB || !__CC65__ */
