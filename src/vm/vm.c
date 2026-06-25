/* VM dispatch loop.
 *
 * Stack-based bytecode interpreter. The Value stack and globals storage
 * are file-statics — cc65 places them in BSS, which lives between the
 * end of the SYS binary and the start of ProDOS file buffers (see
 * docs/contributing/MEMORY_MAP.md § "actual layout").
 *
 * Globals persist across `vm_run` calls so that the REPL can accumulate
 * `let`/`var` bindings across input lines. `vm_reset_globals` is called
 * by file_runner between fresh compiles.
 *
 * Supports: OP_NIL/TRUE/FALSE, OP_INT_U8/INT_I16, OP_STR,
 * OP_POP/DUP/SWAP, OP_GET/SET/DEFINE_GLOBAL, OP_ADD/SUB/MUL/DIV/MOD/NEG/
 * INC/DEC, OP_EQ/NEQ/LT/LE/GT/GE/NOT, OP_HALT, OP_CALL_BUILTIN(print).
 * Anything else traps with SE_BAD_OPCODE so we notice the moment new
 * code emits an opcode without wiring it.
 *
 * Code paths in this file run on both clang (host) and cc65 (target).
 * Stick to C89 declarations: all locals at the top of their block.
 */
#include "vm.h"
#include "opcodes.h"
#include "profile.h"
#include "bcwin.h"

#include "../common/config.h"
#include "../common/zeropage.h"
#include "../compiler/funcs.h"
#include "../compiler/globals.h"
#include "../platform/platform.h"
#include "../runtime/array.h"
#include "../runtime/builtins.h"
#include "../runtime/heap.h"
#include "../runtime/string_pool.h"
#include "../runtime/value.h"
#ifdef WITH_SWB
#include "../runtime/file_io.h"  /* Family B readFile/writeFile builtins */
#include "../runtime/prodos.h"   /* delete/rename/exists/mkdir (doc 017) */
#endif

/* From src/vm/ops/arith.c */
extern int16_t int_mul(int16_t a, int16_t b);
extern swiftii_err_t int_div(int16_t a, int16_t b, int16_t *out);
extern swiftii_err_t int_mod(int16_t a, int16_t b, int16_t *out);

#if defined(WITH_SWIFTSAT)
/* SWIFTSAT generic XLC dispatch (commit 3d): one trampoline +
 * one JMP table at XLC offset 0 (xlc_table.s) handle every
 * builtin id ≥ BUILTIN_XLC_FIRST. The argc transport is the
 * MAIN BSS slot `xlc_argc` (builtins_xlc.c); we set it on the
 * call site, then JSR through the bus-switching trampoline with
 * A = id. See src/platform/apple2/xlc.s + design doc 011. */
extern swiftii_err_t __fastcall__ call_xlc_dispatch(uint8_t id);
extern uint8_t xlc_argc;
extern uint8_t xlc_builtin_id;
#define XLC_CALL(id, argc) (xlc_argc = (argc), call_xlc_dispatch(id))
#elif defined(WITH_SWIFTAUX)
/* SWIFTAUX aux copy-down dispatch: same argc
 * transport, but the trampoline copies the dispatcher body down from
 * the aux park into the main-RAM STAGING buffer (via ROM AUXMOVE) and
 * JSRs it there — see src/platform/apple2/aux_xlc.s + design doc 011
 * stage-2 refresh. No Saturn bank switching; a //e has no Saturn. */
extern swiftii_err_t __fastcall__ aux_xlc_call(uint8_t id);
extern uint8_t xlc_argc;
extern uint8_t xlc_builtin_id;
#define XLC_CALL(id, argc) (xlc_argc = (argc), aux_xlc_call(id))
#elif !defined(__CC65__)
/* Host build: parallel switch-statement dispatch in
 * builtins_xlc.c — no trampoline, no bus switching. */
#include "builtins_xlc.h"
#define XLC_CALL(id, argc) xlc_call_dispatch((id), (argc))
#elif defined(WITH_SWB)
/* Family B Runner: the extras surface (asc/chr/Int(_:)/array
 * methods/platform builtins, ids >= BUILTIN_XLC_FIRST) dispatches to the
 * same plain-C switch the host uses (builtins_xlc.c, normal CODE — no
 * Saturn bank, no aux park, runs on any machine). No XLC_CALL macro:
 * the Runner keeps the lite inline path for core ids/opcodes and routes
 * only the high ids through xlc_call_dispatch (see OP_CALL_BUILTIN). */
#include "builtins_xlc.h"
#endif

/* str_bytes / make_heap_str are file-local except on builds with an
 * XLC path, where a relocated opcode dispatcher (builtins_xlc.c) calls
 * them back in MAIN. Declared in vm.h under the same condition.
 * SWIFTAUX is included because builtins_xlc.c is one TU — even the
 * not-yet-ported dispatchers (e.g. xlc_str_to_int) compile in and
 * reference these, so they must be non-static for the aux link, though
 * SWIFTAUX itself runs the core ops inline in MAIN (the lite path). */
#if defined(WITH_SWIFTSAT) || defined(WITH_SWIFTAUX) || defined(WITH_SWB) || \
    !defined(__CC65__)
#define VM_XLC_SHARED
#else
#define VM_XLC_SHARED static
#endif

/* Dropped `static` so XLC-resident
 * dispatchers (src/vm/builtins_xlc.c) can read+write the stack
 * directly. Paired with vm_sp from common/zeropage.h. Keeping the
 * s_stack/s_globals naming convention (file-local "look") rather than
 * promoting to vm_stack/vm_globals because every reference still
 * lives in vm.c — the XLC-side access is the single, deliberate
 * exception. */
Value s_stack[VM_STACK_SLOTS];
static Value s_globals[MAX_GLOBALS];
static unsigned char s_globals_defined[MAX_GLOBALS];

#if defined(WITH_SWB) && defined(__CC65__)
/* Ctrl-C poll divider for OP_LOOP (Family B Runner only — the one cc65
 * binary that defines WITH_SWB and links the VM). DATA-initialised so the
 * first poll window is full. */
static unsigned char s_break_tick = 64;
#endif

/* Call frame stack. Each frame stores enough to resume the
 * caller after OP_RETURN: the PC to resume at and the frame pointer
 * (stack offset of the first local / first parameter). VM_CALL_FRAMES
 * caps recursion depth (docs/contributing/CONSTRAINTS.md § 5 puts it at 4). */
typedef struct call_frame {
  uint16_t      saved_pc;
  unsigned char saved_fp;
} CallFrame;

static CallFrame    s_frames[VM_CALL_FRAMES];
static unsigned char s_frame_count;

void vm_reset_globals(void) {
  unsigned char i;
  for (i = 0; i < MAX_GLOBALS; ++i) {
    if (s_globals_defined[i]) {
      value_release(&s_globals[i]);
    }
    s_globals_defined[i] = 0;
    s_globals[i].tag = T_NIL;
    s_globals[i].lo = 0;
    s_globals[i].hi = 0;
  }
}

/* Read a string's bytes and length whether it lives in the pool or
 * on the heap. Out-of-range strings come back with len 0 and a NULL
 * data pointer so callers can fail gracefully. */
VM_XLC_SHARED void str_bytes(uint16_t payload, const unsigned char **data,
                      uint16_t *len) {
  const StringPoolEntry *entry;
  if (payload < (uint16_t)STRING_POOL_SLOTS) {
    entry = string_pool_get(payload);
    if (entry == (const StringPoolEntry *)0) {
      *data = (const unsigned char *)0;
      *len = 0;
      return;
    }
    *data = (const unsigned char *)entry->data;
    *len = entry->len;
    return;
  }
  *data = heap_payload((heap_off_t)payload);
  *len = heap_len((heap_off_t)payload);
}

/* Format an int16 into the given buffer (without a NUL). Returns the
 * number of bytes written. `buf` must hold at least 7 bytes (-32768
 * plus possible '-' sign). */
VM_XLC_SHARED uint16_t fmt_i16(int16_t n, unsigned char *buf) {
  unsigned char tmp[7];
  uint16_t un;
  unsigned char idx;
  unsigned char out;
  unsigned char negative;

  if (n < 0) {
    negative = 1;
    un = (uint16_t)(0u - (uint16_t)n);
  } else {
    negative = 0;
    un = (uint16_t)n;
  }
  idx = 0;
  if (un == 0) {
    tmp[idx++] = '0';
  } else {
    while (un > 0) {
      tmp[idx++] = (unsigned char)('0' + (un % 10));
      un = (uint16_t)(un / 10);
    }
  }
  out = 0;
  if (negative) buf[out++] = '-';
  while (idx > 0) {
    buf[out++] = tmp[--idx];
  }
  return (uint16_t)out;
}

/* Allocate a heap string of `n` bytes; copy from `src`. Pushes a
 * T_STR Value onto the caller's stack via `out`. Returns SE_OOM on
 * heap exhaustion. */
VM_XLC_SHARED swiftii_err_t make_heap_str(const unsigned char *src, uint16_t n,
                                   Value *out) {
  heap_off_t off;
  unsigned char *dst;
  uint16_t i;
  off = heap_alloc(n);
  if (off == HEAP_NULL) return SE_OOM;
  dst = heap_payload(off);
  for (i = 0; i < n; ++i) dst[i] = src[i];
  out->tag = T_STR;
  out->lo = (unsigned char)((uint16_t)off & 0xFF);
  out->hi = (unsigned char)(((uint16_t)off >> 8) & 0xFF);
  return SE_OK;
}

unsigned char vm_get_global(uint8_t index, Value *out) {
  if (index >= MAX_GLOBALS) return 0;
  if (!s_globals_defined[index]) return 0;
  *out = s_globals[index];
  return 1;
}

#ifdef WITH_SWB
/* Copy a String Value's bytes into `buf` as a NUL-terminated path, capped
 * at `cap - 1`. The Family B file/dir builtins share this. */
static void swb_value_to_path(const Value *sv, char *buf, uint16_t cap) {
  const unsigned char *pb;
  uint16_t pl;
  uint16_t i;
  str_bytes(VALUE_PAYLOAD_U16(*sv), &pb, &pl);
  if (pl > (uint16_t)(cap - 1)) pl = (uint16_t)(cap - 1);
  for (i = 0; i < pl; ++i) buf[i] = (char)pb[i];
  buf[pl] = '\0';
}

/* Family B file/directory builtins ($26-$2D — doc 015 readFile/writeFile +
 * doc 017 CRUD). Single execution path shared by the cc65 Runner (real
 * ProDOS MLI via prodos.c) and the host (stdio/POSIX), so the host test
 * suite exercises it. Never linked into the at-ceiling SWIFTSAT/SWIFTAUX
 * interpreters — they don't define WITH_SWB. Reads args off the value
 * stack, pushes the result, returns SE_OK or an error. */
static swiftii_err_t vm_file_builtin(unsigned char builtin_id,
                                     unsigned char argc) {
  Value v;
  Value va;
  Value vb;
  char path[64];

  switch (builtin_id) {
    case BUILTIN_READ_FILE: {
      /* readFile(path) -> String?: file bytes, or nil if unopenable. */
      const unsigned char *data;
      int16_t fn;
      swiftii_err_t rc;
      if (argc != 1) return SE_BAD_OPCODE;
      va = s_stack[vm_sp - 1];
      if (va.tag != T_STR) return SE_TYPE_MISMATCH;
      swb_value_to_path(&va, path, sizeof path);
      fn = userfile_read(path, &data);   /* before releasing the arg */
      value_release(&va);
      --vm_sp;
      if (fn < 0) {
        v.tag = T_OPT_NIL; v.lo = 0; v.hi = 0;
      } else {
        rc = make_heap_str(data, (uint16_t)fn, &v);
        if (rc != SE_OK) return rc;
      }
      break;
    }
    case BUILTIN_WRITE_FILE:
    case BUILTIN_APPEND_FILE:
    case BUILTIN_RENAME_FILE: {
      /* writeFile/appendFile(path, contents) and renameFile(old, new):
       * two String args -> Bool. */
      const unsigned char *cb;
      uint16_t cl;
      unsigned char ok;
      if (argc != 2) return SE_BAD_OPCODE;
      vb = s_stack[vm_sp - 1];   /* contents / new path */
      va = s_stack[vm_sp - 2];   /* path */
      if (va.tag != T_STR || vb.tag != T_STR) return SE_TYPE_MISMATCH;
      swb_value_to_path(&va, path, sizeof path);
      if (builtin_id == BUILTIN_RENAME_FILE) {
        char npath[64];
        swb_value_to_path(&vb, npath, sizeof npath);
        ok = (unsigned char)(pf_rename(path, npath) == 0);
      } else {
        str_bytes(VALUE_PAYLOAD_U16(vb), &cb, &cl);
        if (builtin_id == BUILTIN_WRITE_FILE)
          ok = (unsigned char)(userfile_write(path, cb, cl) == 0);
        else
          ok = (unsigned char)(userfile_append(path, cb, cl) == 0);
      }
      value_release(&vb);
      value_release(&va);
      vm_sp = (unsigned char)(vm_sp - 2);
      v.tag = T_BOOL; v.lo = ok; v.hi = 0;
      break;
    }
    case BUILTIN_DELETE_FILE:   /* deleteFile / deleteDirectory ($28) */
    case BUILTIN_FILE_EXISTS:
    case BUILTIN_CREATE_DIR: {
      /* one String path -> Bool. */
      unsigned char ok;
      if (argc != 1) return SE_BAD_OPCODE;
      va = s_stack[vm_sp - 1];
      if (va.tag != T_STR) return SE_TYPE_MISMATCH;
      swb_value_to_path(&va, path, sizeof path);
      if (builtin_id == BUILTIN_FILE_EXISTS)
        ok = (unsigned char)pf_exists(path);
      else if (builtin_id == BUILTIN_DELETE_FILE)
        ok = (unsigned char)(pf_delete(path) == 0);
      else
        ok = (unsigned char)(pf_mkdir(path) == 0);
      value_release(&va);
      --vm_sp;
      v.tag = T_BOOL; v.lo = ok; v.hi = 0;
      break;
    }
    case BUILTIN_LIST_DIR: {
      /* listDirectory(path) -> [String]. A missing/unopenable directory
       * yields the empty array (the one-byte ctype can't carry
       * [String]?, doc 017). The walk runs to completion here, so
       * reusing readFile's block buffer is safe. */
      char name[16];
      unsigned char nl;
      heap_off_t arr;
      if (argc != 1) return SE_BAD_OPCODE;
      va = s_stack[vm_sp - 1];
      if (va.tag != T_STR) return SE_TYPE_MISMATCH;
      swb_value_to_path(&va, path, sizeof path);
      arr = array_new(ARRAY_INIT_CAPACITY);
      if (arr == HEAP_NULL) { value_release(&va); --vm_sp; return SE_OOM; }
      if (userdir_open(path) == 0) {
        while (userdir_next(name, &nl)) {
          Value sv;
          swiftii_err_t rc;
          rc = make_heap_str((const unsigned char *)name, nl, &sv);
          if (rc != SE_OK) {
            userdir_close(); value_release(&va); --vm_sp; return rc;
          }
          rc = array_append(&arr, &sv);
          value_release(&sv);   /* the array holds its own retain */
          if (rc != SE_OK) {
            userdir_close(); value_release(&va); --vm_sp; return rc;
          }
        }
        userdir_close();
      }
      value_release(&va);
      --vm_sp;
      v.tag = T_ARR;
      v.lo = (unsigned char)(arr & 0xFF);
      v.hi = (unsigned char)((arr >> 8) & 0xFF);
      break;
    }
    default:
      return SE_BAD_OPCODE;
  }

  if (vm_sp >= VM_STACK_SLOTS) return SE_STACK_OVER;
  s_stack[vm_sp++] = v;
  return SE_OK;
}
#endif

/* Bytecode access. Without WITH_AUX_BC these expand to the plain in-place
 * reads from `code` (the bytecode pointer into the image), so a non-paged
 * build is byte-identical. With WITH_AUX_BC the bytecode lives in aux and
 * is read through the MAIN window (bcwin.c): BC_ENSURE maps the current
 * instruction in once at the top of the loop, then BC_AT / bc_next read it
 * from the window without further checks. */
#ifdef WITH_AUX_BC
#define BC_ENSURE(pc) bcwin_ensure(pc)
#define BC_AT(i)      (bcwin_buf[(uint16_t)(i) - bcwin_base])
static unsigned char bc_next(const unsigned char *code) {
  unsigned char b;
  (void)code;
  b = BC_AT(vm_pc);
  ++vm_pc;
  return b;
}
#else
#define BC_ENSURE(pc) ((void)0)
#define BC_AT(i)      (code[(i)])
#define bc_next(code) ((code)[vm_pc++])
#endif

swiftii_err_t vm_run(const unsigned char *code, uint16_t start_pc,
                     uint16_t len) {
  Value v;
  Value va;
  Value vb;
  uint16_t idx;
  unsigned char builtin_id;
  unsigned char argc;
  unsigned char gi;
  register int16_t a;
  register int16_t b;
  int16_t r;
  swiftii_err_t rc;

#ifdef WITH_AUX_BC
  (void)code;          /* bytecode is read from the aux window, not `code` */
  bcwin_begin(len);
#endif

  vm_pc = start_pc;
  vm_sp = 0;
  vm_fp = 0;
  s_frame_count = 0;

  PROFILE_INIT();

  while (vm_pc < len) {
    BC_ENSURE(vm_pc);   /* map [vm_pc, vm_pc+2] in (no-op unless WITH_AUX_BC) */
    vm_op = bc_next(code);
    PROFILE_TICK(vm_op);
    switch (vm_op) {

      case OP_HALT:
        return SE_OK;

      case OP_NIL:
        if (vm_sp >= VM_STACK_SLOTS) return SE_STACK_OVER;
        v.tag = T_NIL; v.lo = 0; v.hi = 0;
        s_stack[vm_sp++] = v;
        break;

      case OP_TRUE:
        if (vm_sp >= VM_STACK_SLOTS) return SE_STACK_OVER;
        v.tag = T_BOOL; v.lo = 1; v.hi = 0;
        s_stack[vm_sp++] = v;
        break;

      case OP_FALSE:
        if (vm_sp >= VM_STACK_SLOTS) return SE_STACK_OVER;
        v.tag = T_BOOL; v.lo = 0; v.hi = 0;
        s_stack[vm_sp++] = v;
        break;

      case OP_INT_U8:
        if (vm_pc + 1 > len) return SE_BAD_OPCODE;
        if (vm_sp >= VM_STACK_SLOTS) return SE_STACK_OVER;
        v.tag = T_INT;
        v.lo = bc_next(code);
        v.hi = 0;
        s_stack[vm_sp++] = v;
        break;

      case OP_INT_I16:
        if (vm_pc + 2 > len) return SE_BAD_OPCODE;
        if (vm_sp >= VM_STACK_SLOTS) return SE_STACK_OVER;
        v.tag = T_INT;
        v.lo = bc_next(code);
        v.hi = bc_next(code);
        s_stack[vm_sp++] = v;
        break;

      case OP_STR:
        if (vm_pc + 2 > len) return SE_BAD_OPCODE;
        idx = (uint16_t)BC_AT(vm_pc) | ((uint16_t)BC_AT(vm_pc + 1) << 8);
        vm_pc += 2;
        if (vm_sp >= VM_STACK_SLOTS) return SE_STACK_OVER;
        v.tag = T_STR;
        v.lo = (unsigned char)(idx & 0xFF);
        v.hi = (unsigned char)((idx >> 8) & 0xFF);
        s_stack[vm_sp++] = v;
        /* OP_STR pushes a *new* reference to the constant. Heap-resident
         * constants need a retain so repeated execution of this opcode
         * (e.g. in a loop) does not drop their refcount past zero. Pool
         * refs short-circuit inside value_retain. */
        value_retain(&v);
        break;

      case OP_POP:
        if (vm_sp == 0) return SE_STACK_UNDER;
        --vm_sp;
        value_release(&s_stack[vm_sp]);
        break;

      case OP_DUP:
        if (vm_sp == 0) return SE_STACK_UNDER;
        if (vm_sp >= VM_STACK_SLOTS) return SE_STACK_OVER;
        s_stack[vm_sp] = s_stack[vm_sp - 1];
        value_retain(&s_stack[vm_sp]);
        ++vm_sp;
        break;

      case OP_SWAP:
        if (vm_sp < 2) return SE_STACK_UNDER;
        v = s_stack[vm_sp - 1];
        s_stack[vm_sp - 1] = s_stack[vm_sp - 2];
        s_stack[vm_sp - 2] = v;
        break;

      case OP_OVER:
        /* ( a b -- a b a ): the new copy of `a` is an additional
         * reference, so retain heap-ref `a`. */
        if (vm_sp < 2) return SE_STACK_UNDER;
        if (vm_sp >= VM_STACK_SLOTS) return SE_STACK_OVER;
        s_stack[vm_sp] = s_stack[vm_sp - 2];
        value_retain(&s_stack[vm_sp]);
        ++vm_sp;
        break;

      case OP_DEFINE_GLOBAL:
        if (vm_pc + 1 > len) return SE_BAD_OPCODE;
        gi = bc_next(code);
        if (gi >= MAX_GLOBALS) return SE_BAD_OPCODE;
        if (vm_sp == 0) return SE_STACK_UNDER;
        if (s_globals_defined[gi]) {
          /* Re-DEFINE in the REPL on a subsequent line. Release the
           * previously-stored value before transferring the new one. */
          value_release(&s_globals[gi]);
        }
        /* Transfer ownership from stack to global; no retain/release. */
        s_globals[gi] = s_stack[--vm_sp];
        s_globals_defined[gi] = 1;
        break;

      case OP_GET_GLOBAL:
        if (vm_pc + 1 > len) return SE_BAD_OPCODE;
        gi = bc_next(code);
        if (gi >= MAX_GLOBALS) return SE_BAD_OPCODE;
        if (!s_globals_defined[gi]) return SE_BAD_OPCODE;
        if (vm_sp >= VM_STACK_SLOTS) return SE_STACK_OVER;
        s_stack[vm_sp] = s_globals[gi];
        value_retain(&s_stack[vm_sp]);
        ++vm_sp;
        break;

      case OP_SET_GLOBAL:
        if (vm_pc + 1 > len) return SE_BAD_OPCODE;
        gi = bc_next(code);
        if (gi >= MAX_GLOBALS) return SE_BAD_OPCODE;
        if (!s_globals_defined[gi]) return SE_BAD_OPCODE;
        if (vm_sp == 0) return SE_STACK_UNDER;
        value_release(&s_globals[gi]);
        s_globals[gi] = s_stack[--vm_sp];
        break;

      case OP_ADD:
      case OP_SUB:
      case OP_MUL:
      case OP_DIV:
      case OP_MOD:
        if (vm_sp < 2) return SE_STACK_UNDER;
        vb = s_stack[vm_sp - 1];
        va = s_stack[vm_sp - 2];
        /* String + String dispatches to OP_STR_CONCAT semantics so the
         * compiler can keep emitting OP_ADD for `+` without tracking
         * operand types. the type checker may emit OP_STR_CONCAT
         * directly. */
        if (vm_op == OP_ADD && va.tag == T_STR && vb.tag == T_STR) {
          const unsigned char *da;
          const unsigned char *db;
          uint16_t la;
          uint16_t lb;
          unsigned char tmp[256];
          uint16_t total;
          uint16_t i;
          str_bytes(VALUE_PAYLOAD_U16(va), &da, &la);
          str_bytes(VALUE_PAYLOAD_U16(vb), &db, &lb);
          total = (uint16_t)(la + lb);
          if (total > sizeof(tmp)) return SE_OOM;
          for (i = 0; i < la; ++i) tmp[i] = da[i];
          for (i = 0; i < lb; ++i) tmp[la + i] = db[i];
          value_release(&vb);
          value_release(&va);
          vm_sp = (unsigned char)(vm_sp - 2);
          rc = make_heap_str(tmp, total, &v);
          if (rc != SE_OK) return rc;
          if (vm_sp >= VM_STACK_SLOTS) return SE_STACK_OVER;
          s_stack[vm_sp++] = v;
          break;
        }
        vm_sp = (unsigned char)(vm_sp - 2);
        if (va.tag != T_INT || vb.tag != T_INT) return SE_TYPE_MISMATCH;
        a = (int16_t)((uint16_t)va.lo | ((uint16_t)va.hi << 8));
        b = (int16_t)((uint16_t)vb.lo | ((uint16_t)vb.hi << 8));
        switch (vm_op) {
          case OP_ADD: r = (int16_t)((uint16_t)a + (uint16_t)b); break;
          case OP_SUB: r = (int16_t)((uint16_t)a - (uint16_t)b); break;
          case OP_MUL: r = int_mul(a, b); break;
          case OP_DIV: rc = int_div(a, b, &r); if (rc) return rc; break;
          default:     rc = int_mod(a, b, &r); if (rc) return rc; break;
        }
        v.tag = T_INT;
        v.lo = (unsigned char)((uint16_t)r & 0xFF);
        v.hi = (unsigned char)(((uint16_t)r >> 8) & 0xFF);
        s_stack[vm_sp++] = v;
        break;

      case OP_NEG:
        if (vm_sp == 0) return SE_STACK_UNDER;
        va = s_stack[vm_sp - 1];
        if (va.tag != T_INT) return SE_TYPE_MISMATCH;
        a = (int16_t)((uint16_t)va.lo | ((uint16_t)va.hi << 8));
        r = (int16_t)(0u - (uint16_t)a);
        s_stack[vm_sp - 1].lo = (unsigned char)((uint16_t)r & 0xFF);
        s_stack[vm_sp - 1].hi = (unsigned char)(((uint16_t)r >> 8) & 0xFF);
        break;

      case OP_INC:
        if (vm_sp == 0) return SE_STACK_UNDER;
        if (s_stack[vm_sp - 1].tag != T_INT) return SE_TYPE_MISMATCH;
        a = (int16_t)((uint16_t)s_stack[vm_sp - 1].lo |
                      ((uint16_t)s_stack[vm_sp - 1].hi << 8));
        r = (int16_t)((uint16_t)a + 1u);
        s_stack[vm_sp - 1].lo = (unsigned char)((uint16_t)r & 0xFF);
        s_stack[vm_sp - 1].hi = (unsigned char)(((uint16_t)r >> 8) & 0xFF);
        break;

      /* OP_DEC ($37) reserved but never emitted — id stays in
       * opcodes.h so a future feature can re-add the case without a
       * bytecode-stream renumbering. Falls through to default
       * (SE_BAD_OPCODE) until then. */

      case OP_EQ:
      case OP_NEQ:
        if (vm_sp < 2) return SE_STACK_UNDER;
        vb = s_stack[--vm_sp];
        va = s_stack[--vm_sp];
        {
          unsigned char eq;
          /* nil == nil for both T_NIL and T_OPT_NIL. */
          if ((va.tag == T_NIL || va.tag == T_OPT_NIL) &&
              (vb.tag == T_NIL || vb.tag == T_OPT_NIL)) {
            eq = 1;
          } else if (va.tag != vb.tag) {
            eq = 0;
          } else {
            eq = (va.lo == vb.lo && va.hi == vb.hi) ? 1 : 0;
          }
          v.tag = T_BOOL;
          v.lo = (vm_op == OP_EQ) ? eq : (unsigned char)(1 - eq);
          v.hi = 0;
        }
        s_stack[vm_sp++] = v;
        break;

      case OP_LT:
      case OP_LE:
      case OP_GT:
      case OP_GE:
        if (vm_sp < 2) return SE_STACK_UNDER;
        vb = s_stack[--vm_sp];
        va = s_stack[--vm_sp];
        if (va.tag != T_INT || vb.tag != T_INT) return SE_TYPE_MISMATCH;
        a = (int16_t)((uint16_t)va.lo | ((uint16_t)va.hi << 8));
        b = (int16_t)((uint16_t)vb.lo | ((uint16_t)vb.hi << 8));
        v.tag = T_BOOL;
        v.hi = 0;
        switch (vm_op) {
          case OP_LT: v.lo = (a <  b) ? 1 : 0; break;
          case OP_LE: v.lo = (a <= b) ? 1 : 0; break;
          case OP_GT: v.lo = (a >  b) ? 1 : 0; break;
          default:    v.lo = (a >= b) ? 1 : 0; break;
        }
        s_stack[vm_sp++] = v;
        break;

      case OP_NOT:
        if (vm_sp == 0) return SE_STACK_UNDER;
        if (s_stack[vm_sp - 1].tag != T_BOOL) return SE_TYPE_MISMATCH;
        s_stack[vm_sp - 1].lo = (unsigned char)(1 - s_stack[vm_sp - 1].lo);
        break;

      case OP_JUMP: {
        int16_t off;
        if (vm_pc + 2 > len) return SE_BAD_OPCODE;
        off = (int16_t)((uint16_t)BC_AT(vm_pc) | ((uint16_t)BC_AT(vm_pc + 1) << 8));
        vm_pc += 2;
        vm_pc = (uint16_t)((int16_t)vm_pc + off);
        if (vm_pc > len) return SE_BAD_OPCODE;
        break;
      }

      case OP_JUMP_IF_FALSE:
      case OP_JUMP_IF_TRUE: {
        int16_t off;
        unsigned char take;
        if (vm_sp == 0) return SE_STACK_UNDER;
        if (vm_pc + 2 > len) return SE_BAD_OPCODE;
        vb = s_stack[--vm_sp];
        if (vb.tag != T_BOOL) return SE_TYPE_MISMATCH;
        off = (int16_t)((uint16_t)BC_AT(vm_pc) | ((uint16_t)BC_AT(vm_pc + 1) << 8));
        vm_pc += 2;
        if (vm_op == OP_JUMP_IF_FALSE) take = (unsigned char)(!vb.lo);
        else                        take = (unsigned char)(vb.lo);
        if (take) {
          vm_pc = (uint16_t)((int16_t)vm_pc + off);
          if (vm_pc > len) return SE_BAD_OPCODE;
        }
        break;
      }

      case OP_LOOP: {
        uint16_t off;
#if defined(WITH_SWB) && defined(__CC65__)
        /* Family B Runner: Ctrl-C breaks a running program. Every infinite
         * loop passes through this backward jump (recursion is capped by
         * call depth), so polling here catches all hangs at zero cost to
         * straight-line code; the /64 tick keeps the keyboard read off the
         * tightest loops. Family A interpreters define neither macro and
         * stay byte-identical. (While the program is BLOCKED in readLine,
         * Ctrl-C is just input — this breaks running code, not waits.) */
        if (--s_break_tick == 0) {
          s_break_tick = 64;
          if (*(volatile unsigned char *)0xC000 == 0x83) {  /* Ctrl-C */
            *(volatile unsigned char *)0xC010 = 0;          /* clear strobe */
            return SE_BREAK;
          }
        }
#endif
        if (vm_pc + 2 > len) return SE_BAD_OPCODE;
        off = (uint16_t)BC_AT(vm_pc) | ((uint16_t)BC_AT(vm_pc + 1) << 8);
        vm_pc += 2;
        if (off > vm_pc) return SE_BAD_OPCODE;
        vm_pc = (uint16_t)(vm_pc - off);
        break;
      }

      case OP_CALL_BUILTIN:
        if (vm_pc + 2 > len) return SE_BAD_OPCODE;
        builtin_id = bc_next(code);
        argc = bc_next(code);
        if (vm_sp < argc) return SE_STACK_UNDER;
#ifdef WITH_SWB
        /* Family B file/dir builtins ($26-$2D) run the same on the Runner
         * and the host — intercept before the XLC routing split. The upper
         * bound matters: the system builtins (wait/exit/heapAvailable) sit
         * at higher ids ($2E+) and must fall through to the routing split,
         * not be mistaken for file ops here. */
        if (builtin_id >= BUILTIN_READ_FILE && builtin_id <= BUILTIN_LIST_DIR) {
          rc = vm_file_builtin(builtin_id, argc);
          if (rc != SE_OK) return rc;
          break;
        }
#endif
#if defined(WITH_SWIFTSAT) || defined(WITH_SWIFTAUX) || !defined(__CC65__)
        /* SWIFTSAT + SWIFTAUX route every builtin through the XLC
         * trampoline so the fat cold core dispatcher (print/print_t/
         * readLine/min/max) lives out of MAIN. The XLC-only builtins
         * (asc/chr/Int(s)/array methods, ids ≥ BUILTIN_XLC_FIRST) hit
         * their own slot; the core builtins (ids < BUILTIN_XLC_FIRST)
         * share xlc_call_builtin_dispatch, with the specific id passed
         * via the xlc_builtin_id transport. On SWIFTSAT the bodies run
         * in place in Saturn bank 1; on SWIFTAUX they're copied down
         * from the aux park into STAGING per call (asc/chr + call_builtin
         * are the slice-2 overlays — the hot ops STR_CONCAT/INTERP/
         * NEW_ARRAY/ARR_LEN deliberately stay inline in MAIN below to
         * avoid a per-call AUXMOVE). Host runs the dispatchers in normal
         * CODE. */
        /* The system builtins (ids >= BUILTIN_SYS_FIRST) deliberately have
         * NO per-id table slot — they ride the shared core-builtin
         * dispatcher (the `else` arm) via the xlc_builtin_id transport, so
         * they need no xlc_table.s slot and no SWIFTAUX copy-down overlay
         * entry. wait() (the only sys builtin) ships on cc65 ONLY via the
         * Family B Runner (the lite path below, not this extras path), so on
         * the extras REPLs (SWIFTSAT/SWIFTAUX) this bound is compiled out and
         * they stay byte-identical; it's active only on host. */
        if (builtin_id >= BUILTIN_XLC_FIRST
#if !defined(__CC65__)
            && builtin_id < BUILTIN_SYS_FIRST
#endif
           ) {
#if defined(WITH_SWIFTAUX)
          /* SWIFTAUX groups the platform builtins ($18-$24) into two
           * copy-down overlays whose entry switches on xlc_builtin_id, so
           * stash the real id here too. Harmless for the ungrouped aux
           * dispatchers (asc/chr/Int/array/str_interp), which ignore it.
           * Gated to aux so SWIFTSAT/host (per-id table slots) stay
           * byte-identical. */
          xlc_builtin_id = builtin_id;
#endif
          rc = XLC_CALL(builtin_id, argc);
        } else {
          xlc_builtin_id = builtin_id;
          rc = XLC_CALL(XLC_OP_CALL_BUILTIN, argc);
        }
        if (rc != SE_OK) return rc;
#else
        /* Lite: bodies stay inline in MAIN (no XLC).
         * Kept byte-for-byte in sync with xlc_call_builtin_dispatch in
         * builtins_xlc.c. These binaries have no XLC builtins, so an
         * unknown id falls through to the final SE_BAD_OPCODE. */
        if (builtin_id == BUILTIN_PRINT) {
          unsigned char i;
          for (i = 0; i < argc; ++i) {
            builtins_print_value(&s_stack[vm_sp - argc + i]);
          }
          builtins_print_newline();
          /* Release any heap refs we consumed. */
          for (i = 0; i < argc; ++i) {
            value_release(&s_stack[vm_sp - argc + i]);
          }
          vm_sp = (unsigned char)(vm_sp - argc);
          if (vm_sp >= VM_STACK_SLOTS) return SE_STACK_OVER;
          v.tag = T_NIL; v.lo = 0; v.hi = 0;
          s_stack[vm_sp++] = v;
        } else if (builtin_id == BUILTIN_PRINT_T) {
          /* print(_:terminator:) — the LAST arg is the terminator.
           * Print the leading args, then the terminator literally
           * (no implicit '\n'). Non-string terminators are a runtime
           * type error. */
          unsigned char i;
          Value *term;
          if (argc == 0) return SE_BAD_OPCODE;
          term = &s_stack[vm_sp - 1];
          if (term->tag != T_STR) return SE_TYPE_MISMATCH;
          for (i = 0; i < (unsigned char)(argc - 1); ++i) {
            builtins_print_value(&s_stack[vm_sp - argc + i]);
          }
          /* Print the terminator string bytes directly (without the
           * trailing newline that BUILTIN_PRINT would add). */
          builtins_print_value(term);
          for (i = 0; i < argc; ++i) {
            value_release(&s_stack[vm_sp - argc + i]);
          }
          vm_sp = (unsigned char)(vm_sp - argc);
          if (vm_sp >= VM_STACK_SLOTS) return SE_STACK_OVER;
          v.tag = T_NIL; v.lo = 0; v.hi = 0;
          s_stack[vm_sp++] = v;
        } else if (builtin_id == BUILTIN_READLINE) {
          /* Read one line from stdin; return String? — T_STR on
           * success (trailing '\n' stripped), T_OPT_NIL at EOF. */
          char linebuf[256];
          int16_t n;
          if (argc != 0) return SE_BAD_OPCODE;
          n = platform_read_line(linebuf, (uint16_t)sizeof(linebuf));
#if defined(WITH_SWB) && defined(__CC65__)
          /* Runner: Ctrl-C while blocked in readLine breaks the program,
           * mirroring the OP_LOOP poll. A negative count is the keyboard
           * backend's PLATFORM_READ_BREAK sentinel (the only way n goes < 0;
           * EOF is 0, real lines > 0), and the sign test is cheaper than a
           * 16-bit compare in this byte-tight Runner. Host stays deterministic
           * and the lite inline body stays byte-identical (both un-gated). */
          if (n < 0) return SE_BREAK;
#endif
          if (vm_sp >= VM_STACK_SLOTS) return SE_STACK_OVER;
          if (n <= 0) {
            /* EOF — push none. */
            v.tag = T_OPT_NIL; v.lo = 0; v.hi = 0;
            s_stack[vm_sp++] = v;
          } else {
            uint16_t real_len;
            /* Strip the trailing '\n' if fgets included it (host
             * backend does; the //+ keyboard backend does not). */
            real_len = (uint16_t)n;
            if (real_len > 0 && linebuf[real_len - 1] == '\n') {
              --real_len;
            }
            rc = make_heap_str((const unsigned char *)linebuf,
                               real_len, &v);
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
          r = (builtin_id == BUILTIN_MIN) ? (a < b ? a : b)
                                          : (a > b ? a : b);
          vm_sp = (unsigned char)(vm_sp - 2);
          v.tag = T_INT;
          v.lo = (unsigned char)((uint16_t)r & 0xFF);
          v.hi = (unsigned char)(((uint16_t)r >> 8) & 0xFF);
          s_stack[vm_sp++] = v;
#if defined(WITH_SWB)
        } else if (builtin_id == BUILTIN_ABS || builtin_id == BUILTIN_SGN) {
          /* abs / sgn — pure Int -> Int, Family B Runner inline (lite has
           * no WITH_SWB so it's compiled out and stays byte-identical; host
           * keeps a byte-identical copy in builtins_xlc.c). One Int arg,
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
          /* random(in: a..<b) / a...b -> Int (Family B Runner).
           * Worker (the LCG) is shared with the host's xlc dispatch. */
          if (argc != 2) return SE_BAD_OPCODE;
          vb = s_stack[vm_sp - 1];
          va = s_stack[vm_sp - 2];
          if (va.tag != T_INT || vb.tag != T_INT) return SE_TYPE_MISMATCH;
          rc = builtin_random_in((int16_t)VALUE_PAYLOAD_I16(va),
                                 (int16_t)VALUE_PAYLOAD_I16(vb),
                                 (unsigned char)(builtin_id == BUILTIN_RANDOM_LE),
                                 &v);
          if (rc != SE_OK) return rc;
          vm_sp = (unsigned char)(vm_sp - 2);
          s_stack[vm_sp++] = v;
#endif
#if defined(WITH_80COL) && defined(WITH_IIE) && !defined(WITH_SWB)
        } else if (builtin_id == BUILTIN_TEXT80 || builtin_id == BUILTIN_TEXT) {
          /* Lite //e (SWIFTIIE) toggles 80-column mode inline
           * (no XLC overlay). text80() -> 80 cols (no-op without a card),
           * text() -> 40 cols. Both push nil. Workers in screen.c (MAIN).
           * The //e Family B Runner (WITH_SWB) skips this: its text() must
           * also revert GR mode, so it routes through xlc_text_dispatch
           * via the catch-all below. */
          if (argc != 0) return SE_BAD_OPCODE;
          if (builtin_id == BUILTIN_TEXT80) platform_text80();
          else platform_text40();
          if (vm_sp >= VM_STACK_SLOTS) return SE_STACK_OVER;
          v.tag = T_NIL; v.lo = 0; v.hi = 0;
          s_stack[vm_sp++] = v;
#endif
#if defined(WITH_FILE_CRUD)
        } else if (builtin_id == BUILTIN_WAIT) {
          /* wait(_ ms: Int) -> Void — busy-wait ~ms ms (ROM WAIT loop in
           * platform_wait_ms), push nil. Inline on the Family B Runners
           * (WITH_FILE_CRUD), which take this lite path; kept byte-identical
           * to the xlc_call_builtin_dispatch copy used by SWIFTAUX + host. The
           * lite REPLs ship no wait() (no platform table / no graphics to
           * pace), so they never emit BUILTIN_WAIT. */
          if (argc != 1) return SE_BAD_OPCODE;
          va = s_stack[vm_sp - 1];
          if (va.tag != T_INT) return SE_TYPE_MISMATCH;
          platform_wait_ms(VALUE_PAYLOAD_U16(va));
          s_stack[vm_sp - 1].tag = T_NIL;
          s_stack[vm_sp - 1].lo = 0;
          s_stack[vm_sp - 1].hi = 0;
        } else if (builtin_id == BUILTIN_TONE) {
          /* tone(_ halfPeriod: Int, _ cycles: Int) -> Void — square-wave
           * speaker tone (platform_tone toggles $C030), push nil. Inline on
           * the Family B Runners (WITH_FILE_CRUD); kept byte-identical to the
           * xlc_call_builtin_dispatch copy used by the host. The REPLs ship no
           * tone(). ( halfPeriod cycles -- nil ). */
          if (argc != 2) return SE_BAD_OPCODE;
          vb = s_stack[vm_sp - 1];
          va = s_stack[vm_sp - 2];
          if (va.tag != T_INT || vb.tag != T_INT) return SE_TYPE_MISMATCH;
          platform_tone(VALUE_PAYLOAD_U16(va), VALUE_PAYLOAD_U16(vb));
          vm_sp = (unsigned char)(vm_sp - 2);
          if (vm_sp >= VM_STACK_SLOTS) return SE_STACK_OVER;
          v.tag = T_NIL; v.lo = 0; v.hi = 0;
          s_stack[vm_sp++] = v;
        } else if (builtin_id == BUILTIN_HAS_PREFIX ||
                   builtin_id == BUILTIN_HAS_SUFFIX) {
          /* hasPrefix(t)/hasSuffix(t) -> Bool — Family B Runner inline
           * (byte-identical to the host copy in builtins_xlc.c; both call
           * the shared xlc_has_affix worker). ( receiver needle -- bool );
           * both T_STR, both released. */
          if (argc != 2) return SE_BAD_OPCODE;
          vb = s_stack[vm_sp - 1];
          va = s_stack[vm_sp - 2];
          if (va.tag != T_STR || vb.tag != T_STR) return SE_TYPE_MISMATCH;
          v.tag = T_BOOL;
          v.lo = (unsigned char)xlc_has_affix(
                     VALUE_PAYLOAD_U16(va), VALUE_PAYLOAD_U16(vb),
                     (unsigned char)(builtin_id == BUILTIN_HAS_SUFFIX));
          v.hi = 0;
          value_release(&vb);
          value_release(&va);
          vm_sp = (unsigned char)(vm_sp - 2);
          s_stack[vm_sp++] = v;
#endif
#ifdef WITH_SWB
        } else if (builtin_id >= BUILTIN_XLC_FIRST) {
          /* Family B Runner: the extras surface (asc/chr/Int(_:)/array
           * methods/platform builtins) plus the system builtins (wait/exit/
           * heapAvailable, $2E+) dispatch to the plain-C switch in
           * builtins_xlc.c — compiled as normal CODE here, so it runs on any
           * machine (no Saturn bank / aux park). The file/dir builtins
           * ($26-$2D) sit between those two ranges but were intercepted
           * before this switch (vm_file_builtin); everything else lands
           * here. */
          rc = xlc_call_dispatch(builtin_id, argc);
          if (rc != SE_OK) return rc;
#endif
        } else {
          return SE_BAD_OPCODE;
        }
#endif
        break;

      case OP_STR_CONCAT: {
#if defined(WITH_SWIFTSAT) || defined(WITH_SWIFTAUX) || !defined(__CC65__)
        /* Body relocated to XLC to reclaim MAIN headroom — Saturn bank 1
         * on SWIFTSAT, an XLCSCC copy-down overlay on SWIFTAUX (slice 3
         * step 2, evicted to fund the platform-builtin parser table; the
         * directory park copies only its ~200 B per call). Host routes
         * through the same dispatcher in normal CODE. The dispatcher
         * reads/writes s_stack + vm_sp and calls str_bytes / make_heap_str
         * back in MAIN. argc is unused (opcode, not a builtin call) — pass
         * 0. See builtins_xlc.c:xlc_str_concat_dispatch + design doc 011. */
        rc = XLC_CALL(XLC_OP_STR_CONCAT, 0);
        if (rc != SE_OK) return rc;
#else
        /* Lite have no XLC path, so the body stays
         * inline in MAIN. Kept byte-for-byte in sync with the XLC copy
         * in builtins_xlc.c — this is the duplication cost of
         * relocating a *core* opcode (vs an XLC-only builtin like asc,
         * which has a single copy because lite simply lacks it). */
        const unsigned char *da;
        const unsigned char *db;
        uint16_t la;
        uint16_t lb;
        uint16_t pa;
        uint16_t pb;
        if (vm_sp < 2) return SE_STACK_UNDER;
        vb = s_stack[vm_sp - 1];
        va = s_stack[vm_sp - 2];
        if (va.tag != T_STR || vb.tag != T_STR) return SE_TYPE_MISMATCH;
        pa = VALUE_PAYLOAD_U16(va);
        pb = VALUE_PAYLOAD_U16(vb);
        str_bytes(pa, &da, &la);
        str_bytes(pb, &db, &lb);
        {
          /* Build the concatenation in a small scratch then copy out
           * — we can't write directly into the new heap block while
           * reading from another, because the read pointer may move
           * if the alloc forces a heap-pointer recompute (it doesn't
           * today, but it's defensive against future free-list work). */
          unsigned char tmp[256];
          uint16_t total;
          uint16_t i;
          total = (uint16_t)(la + lb);
          if (total > sizeof(tmp)) return SE_OOM;
          for (i = 0; i < la; ++i) tmp[i] = da[i];
          for (i = 0; i < lb; ++i) tmp[la + i] = db[i];
          /* Release both inputs first (LIFO reclaim friendly) before
           * allocating the result. */
          value_release(&vb);
          value_release(&va);
          --vm_sp; --vm_sp;
          rc = make_heap_str(tmp, total, &v);
          if (rc != SE_OK) return rc;
          if (vm_sp >= VM_STACK_SLOTS) return SE_STACK_OVER;
          s_stack[vm_sp++] = v;
        }
#endif
        break;
      }

      /* OP_STR_INTERP_B ($83) / OP_STR_INTERP_O ($84) reserved but
       * never emitted — the compiler uses OP_STR_INTERP_I ($82) for
       * every operand type because the converter below is already
       * polymorphic. Ids stay in opcodes.h for a future per-type
       * specialization to claim without bytecode renumbering; until
       * then they fall through to default (SE_BAD_OPCODE). */
      case OP_STR_INTERP_I: {
#if defined(WITH_SWIFTSAT) || defined(WITH_SWIFTAUX) || !defined(__CC65__)
        /* Body relocated to XLC (cold — runs once per interpolated
         * operand / String(n), never in a tight loop). On SWIFTSAT it's
         * Saturn bank 1; on SWIFTAUX its own XLCSIP copy-down overlay
         * (slice 3 step 1, evicted from MAIN to fund the Int/array
         * recognizers). See builtins_xlc.c:xlc_str_interp_dispatch. */
        rc = XLC_CALL(XLC_OP_STR_INTERP, 0);
        if (rc != SE_OK) return rc;
#else
        /* Lite: body stays inline in MAIN (no XLC).
         * Kept byte-for-byte in sync with the XLC copy. Polymorphic
         * int/bool/nil/str → heap-string for interpolation; T_STR is a
         * pass-through. */
        unsigned char tmp[7];
        const unsigned char *text;
        uint16_t n;
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
            /* Pass through: leave TOS alone. */
            break;
          default:
            return SE_TYPE_MISMATCH;
        }
        if (va.tag != T_STR) {
          --vm_sp;
          rc = make_heap_str(text, n, &v);
          if (rc != SE_OK) return rc;
          if (vm_sp >= VM_STACK_SLOTS) return SE_STACK_OVER;
          s_stack[vm_sp++] = v;
        }
#endif
        break;
      }

      case OP_UNWRAP:
        /* Force-unwrap: `x!`. Runtime-error if the top of stack is
         * nil; otherwise leave the value alone. */
        if (vm_sp == 0) return SE_STACK_UNDER;
        if (IS_NIL_VAL(s_stack[vm_sp - 1])) return SE_RUNTIME;
        break;

      case OP_NIL_COALESCE:
      case OP_IF_LET: {
        /* These two are stack-effect mirrors:
         *   OP_NIL_COALESCE (lhs ?? rhs): non-nil → keep & jump
         *                                  nil    → pop & fall through
         *   OP_IF_LET (if let v = opt):   non-nil → keep & fall through
         *                                  nil    → pop & jump
         * Consolidated into one case to keep the binary small;
         * the only difference is the direction of the conditional
         * jump (`taken_when_nil = (vm_op == OP_IF_LET)`). */
        int16_t off;
        unsigned char taken_when_nil;
        if (vm_sp == 0) return SE_STACK_UNDER;
        if (vm_pc + 2 > len) return SE_BAD_OPCODE;
        off = (int16_t)((uint16_t)BC_AT(vm_pc) | ((uint16_t)BC_AT(vm_pc + 1) << 8));
        vm_pc += 2;
        taken_when_nil = (unsigned char)(vm_op == OP_IF_LET);
        if (IS_NIL_VAL(s_stack[vm_sp - 1])) {
          --vm_sp;  /* pop the nil (value_release is a no-vm_op for nils) */
          if (taken_when_nil) {
            vm_pc = (uint16_t)((int16_t)vm_pc + off);
            if (vm_pc > len) return SE_BAD_OPCODE;
          }
        } else {
          /* Non-nil: keep on TOS. */
          if (!taken_when_nil) {
            vm_pc = (uint16_t)((int16_t)vm_pc + off);
            if (vm_pc > len) return SE_BAD_OPCODE;
          }
        }
        break;
      }

      case OP_GET_LOCAL:
        if (vm_pc + 1 > len) return SE_BAD_OPCODE;
        gi = bc_next(code);
        /* Local slot offset is vm_fp + gi; bounds-checked against vm_sp so a
         * malformed program can't read past live stack. */
        if ((unsigned char)(vm_fp + gi) >= vm_sp) return SE_STACK_UNDER;
        if (vm_sp >= VM_STACK_SLOTS) return SE_STACK_OVER;
        s_stack[vm_sp] = s_stack[vm_fp + gi];
        value_retain(&s_stack[vm_sp]);
        ++vm_sp;
        break;

      case OP_SET_LOCAL:
        if (vm_pc + 1 > len) return SE_BAD_OPCODE;
        gi = bc_next(code);
        if ((unsigned char)(vm_fp + gi) >= vm_sp) return SE_STACK_UNDER;
        if (vm_sp == 0) return SE_STACK_UNDER;
        value_release(&s_stack[vm_fp + gi]);
        s_stack[vm_fp + gi] = s_stack[--vm_sp];
        break;

      case OP_CALL: {
        /* Operand: u8 fn_idx, u8 argc.
         * Stack before: [ ..., arg0, arg1, ..., arg(N-1) ] with vm_sp at top.
         * Set new vm_fp = vm_sp - argc; jump to function body. The arguments
         * become locals[0..argc-1] of the callee. */
        unsigned char fn_idx;
        unsigned char call_argc;
        uint16_t target;
        if (vm_pc + 2 > len) return SE_BAD_OPCODE;
        fn_idx = bc_next(code);
        call_argc = bc_next(code);
        if (vm_sp < call_argc) return SE_STACK_UNDER;
        target = funcs_get_start(fn_idx);
        if (target == (uint16_t)0xFFFF) return SE_BAD_OPCODE;
        if (target >= len) return SE_BAD_OPCODE;
        if (s_frame_count >= VM_CALL_FRAMES) return SE_STACK_OVER;
        if (funcs_get_param_count(fn_idx) != call_argc) {
          /* Compiler should have caught this — defensive. */
          return SE_BAD_OPCODE;
        }
        s_frames[s_frame_count].saved_pc = vm_pc;
        s_frames[s_frame_count].saved_fp = vm_fp;
        ++s_frame_count;
        vm_fp = (unsigned char)(vm_sp - call_argc);
        vm_pc = target;
        break;
      }

      case OP_RETURN: {
        /* Pop the return value, drop locals + args, restore caller
         * frame, then push the return value at the new TOS. */
        Value retval;
        unsigned char i;
        if (s_frame_count == 0) return SE_BAD_OPCODE;
        if (vm_sp == 0) return SE_STACK_UNDER;
        retval = s_stack[--vm_sp];
        /* Release any heap refs sitting between vm_fp and vm_sp (locals
         * declared in the function body) so they don't leak. */
        for (i = vm_fp; i < vm_sp; ++i) {
          value_release(&s_stack[i]);
        }
        vm_sp = vm_fp;
        --s_frame_count;
        vm_pc = s_frames[s_frame_count].saved_pc;
        vm_fp = s_frames[s_frame_count].saved_fp;
        if (vm_sp >= VM_STACK_SLOTS) return SE_STACK_OVER;
        s_stack[vm_sp++] = retval;
        break;
      }

      case OP_RETURN_V: {
        /* Void return — drop locals + args, restore caller frame,
         * push T_NIL so callers that wrote `f()` as an expression
         * statement can discard or print it. */
        unsigned char i;
        if (s_frame_count == 0) return SE_BAD_OPCODE;
        for (i = vm_fp; i < vm_sp; ++i) {
          value_release(&s_stack[i]);
        }
        vm_sp = vm_fp;
        --s_frame_count;
        vm_pc = s_frames[s_frame_count].saved_pc;
        vm_fp = s_frames[s_frame_count].saved_fp;
        if (vm_sp >= VM_STACK_SLOTS) return SE_STACK_OVER;
        v.tag = T_NIL; v.lo = 0; v.hi = 0;
        s_stack[vm_sp++] = v;
        break;
      }

      case OP_NEW_ARRAY: {
        /* `OP_NEW_ARRAY n`: pop n values, allocate an array with
         * capacity n, move them into the slots (ownership
         * transfers; no retain/release happens in array_init_
         * from_stack). The new array starts with refcount 1.
         * Cold (literal construction) — body relocated to XLC on
         * SWIFTSAT to reclaim MAIN. The operand `n` rides the dispatch
         * arg slot. See builtins_xlc.c:xlc_new_array_dispatch. */
        unsigned char n;
        if (vm_pc + 1 > len) return SE_BAD_OPCODE;
        n = bc_next(code);
#if defined(WITH_SWIFTSAT) || defined(WITH_SWIFTAUX) || !defined(__CC65__)
        rc = XLC_CALL(XLC_OP_NEW_ARRAY, n);
        if (rc != SE_OK) return rc;
#else
        /* Lite: body stays inline (no XLC). Kept
         * byte-for-byte in sync with the XLC copy. */
        {
          heap_off_t arr;
          if (vm_sp < n) return SE_STACK_UNDER;
          arr = array_new((uint16_t)n);
          if (arr == HEAP_NULL) return SE_OOM;
          array_init_from_stack(arr, &s_stack[vm_sp - n], n);
          vm_sp = (unsigned char)(vm_sp - n);
          if (vm_sp >= VM_STACK_SLOTS) return SE_STACK_OVER;
          v.tag = T_ARR;
          v.lo = (unsigned char)(arr & 0xFF);
          v.hi = (unsigned char)((arr >> 8) & 0xFF);
          s_stack[vm_sp++] = v;
        }
#endif
        break;
      }

      case OP_ARR_GET: {
        /* ( arr i -- element ) */
        heap_off_t arr;
        uint16_t idx16;
        if (vm_sp < 2) return SE_STACK_UNDER;
        vb = s_stack[vm_sp - 1];   /* index */
        va = s_stack[vm_sp - 2];   /* array */
        if (va.tag != T_ARR || vb.tag != T_INT) return SE_TYPE_MISMATCH;
        {
          int16_t i_signed = (int16_t)VALUE_PAYLOAD_I16(vb);
          if (i_signed < 0) return SE_RUNTIME;
          idx16 = (uint16_t)i_signed;
        }
        arr = (heap_off_t)VALUE_PAYLOAD_U16(va);
        rc = array_get(arr, idx16, &v);
        if (rc != SE_OK) return rc;
        value_retain(&v);
        /* Pop array (releasing ref) and index, push element. */
        value_release(&va);
        vm_sp = (unsigned char)(vm_sp - 2);
        s_stack[vm_sp++] = v;
        break;
      }

      case OP_ARR_SET: {
        /* ( arr i v -- )  — overwrite slot `i`. Arrays never grow on
         * set, so unlike OP_ARR_APPEND there's no write-back; the
         * stored heap_off_t in the caller variable is still valid. */
        heap_off_t arr;
        uint16_t idx16;
        Value vv;
        if (vm_sp < 3) return SE_STACK_UNDER;
        vv = s_stack[vm_sp - 1];   /* value */
        vb = s_stack[vm_sp - 2];   /* index */
        va = s_stack[vm_sp - 3];   /* array */
        if (va.tag != T_ARR || vb.tag != T_INT) return SE_TYPE_MISMATCH;
        {
          int16_t i_signed = (int16_t)VALUE_PAYLOAD_I16(vb);
          if (i_signed < 0) return SE_RUNTIME;
          idx16 = (uint16_t)i_signed;
        }
        arr = (heap_off_t)VALUE_PAYLOAD_U16(va);
        rc = array_set(arr, idx16, &vv);
        if (rc != SE_OK) return rc;
        /* array_set retained the new slot value, so drop our stack
         * ref to it; same for the array reference we no longer carry. */
        value_release(&vv);
        value_release(&va);
        vm_sp = (unsigned char)(vm_sp - 3);
        break;
      }

      case OP_ARR_LEN: {
        /* ( arr -- int )  — `.count`. Cold; body relocated to XLC on
         * SWIFTSAT (Saturn bank 1) and SWIFTAUX (XLCALN copy-down overlay,
         * slice 3 step 2). See builtins_xlc.c:xlc_arr_len_dispatch. */
#if defined(WITH_SWIFTSAT) || defined(WITH_SWIFTAUX) || !defined(__CC65__)
        rc = XLC_CALL(XLC_OP_ARR_LEN, 0);
        if (rc != SE_OK) return rc;
#else
        /* Lite: inline (no XLC). Kept in sync. Handles `.count` on both
         * arrays and strings (`.isEmpty` desugars to `.count == 0`). */
        uint16_t cnt;
        if (vm_sp == 0) return SE_STACK_UNDER;
        va = s_stack[vm_sp - 1];
        if (va.tag == T_ARR) {
          cnt = array_count((heap_off_t)VALUE_PAYLOAD_U16(va));
        } else if (va.tag == T_STR) {
          /* String length via str_bytes. Building the offset with the
           * `lo | hi<<8` arithmetic miscompiles here under cc65 -Or (the
           * high byte gets OR'd with stale scratch left by the `va`
           * struct-copy load). Reinterpret the two bytes as a word
           * instead — a pure memory combine cc65 gets right. */
          union { unsigned char b[2]; uint16_t w; } pl;
          const unsigned char *sdata;
          pl.b[0] = va.lo;
          pl.b[1] = va.hi;
          str_bytes(pl.w, &sdata, &cnt);
        } else {
          return SE_TYPE_MISMATCH;
        }
        value_release(&va);
        s_stack[vm_sp - 1].tag = T_INT;
        s_stack[vm_sp - 1].lo = (unsigned char)(cnt & 0xFF);
        s_stack[vm_sp - 1].hi = (unsigned char)((cnt >> 8) & 0xFF);
#endif
        break;
      }

      case OP_ARR_APPEND: {
        /* ( arr v -- arr' )  — append `v` to `arr`. If the underlying
         * heap block had to grow, `arr_append` allocates a fresh
         * block and the array Value pushed onto TOS reflects the new
         * heap offset; otherwise it's the same offset that came in.
         *
         * The compiler at statement level is required to emit a
         * write-back (`OP_SET_GLOBAL` / `OP_SET_LOCAL`) right after
         * `OP_ARR_APPEND` so the user-visible variable tracks the
         * (possibly new) buffer. `.append` in expression position is
         * a compile error — see parse_statement and pratt's postfix
         * dot-member handler. */
        heap_off_t arr;
        Value vv;
        if (vm_sp < 2) return SE_STACK_UNDER;
        vv = s_stack[vm_sp - 1];
        va = s_stack[vm_sp - 2];
        if (va.tag != T_ARR) return SE_TYPE_MISMATCH;
        arr = (heap_off_t)VALUE_PAYLOAD_U16(va);
        rc = array_append(&arr, &vv);
        if (rc != SE_OK) return rc;
        /* array_append already heap_release'd the old block when it
         * reallocated. If it didn't reallocate, `arr` is unchanged.
         * We move the (possibly new) reference into TOS without
         * retain/release: ownership is transferring from caller's
         * stack-slot to caller's stack-slot. The `value_release(&vv)`
         * is for the appended value; array_append already retained
         * for the new slot, so we drop our stack ref here. */
        value_release(&vv);
        vm_sp = (unsigned char)(vm_sp - 2);
        if (vm_sp >= VM_STACK_SLOTS) return SE_STACK_OVER;
        v.tag = T_ARR;
        v.lo = (unsigned char)(arr & 0xFF);
        v.hi = (unsigned char)((arr >> 8) & 0xFF);
        s_stack[vm_sp++] = v;
        break;
      }

      default:
        return SE_BAD_OPCODE;
    }
  }

  /* Fell off the end without OP_HALT. */
  return SE_BAD_OPCODE;
}
