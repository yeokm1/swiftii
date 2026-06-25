# 002 - Heap allocator and heap-resident strings

## Problem

Phase 3 introduces user programs that create new strings at runtime
(string concatenation `a + b`, string interpolation `"x = \(x)"`,
escape-processed literals). Phase 0/1's string handling cannot
represent these:

- The `string_pool` holds compile-time-known constants in RODATA. It
  is read-only and bounded to `STRING_POOL_SLOTS` (4 today).
- The `T_STR` Value payload is currently a small pool index. There is
  no mechanism to refer to a heap-allocated string.
- There is no heap allocator at all (`heap.c` is a placeholder), so
  there's nowhere to put a string built at runtime.

Phase 3 also adds `Array` (per `LANGUAGE.md`) eventually, but the
roadmap defers arrays to Phase 4; this proposal therefore covers only
strings for now, with the allocator designed to extend later.

## Proposal

Add a small fixed-size BSS heap (configurable via `HEAP_SIZE`, default
2 KB) with a bump allocator and last-block release optimization, plus
reference counting on heap-allocated `T_STR` values.

- Heap layout: a single `unsigned char s_heap[HEAP_SIZE]` array.
- Each allocation has a 4-byte header (`refcount: u16`, `payload_len: u16`)
  followed by the payload. Headers are little-endian on both host and
  target.
- Allocator: bump pointer (`s_heap_ptr`). On `heap_release`, if the
  released block is the topmost allocation, rewind the bump pointer
  (LIFO reclaim). Otherwise the block becomes garbage we never reuse
  in Phase 3. Segregated free lists are deferred to a later phase.
- Reference counting: heap object refcount starts at 1 on alloc.
  `heap_retain` increments, `heap_release` decrements; at zero the
  block is reclaimed if topmost, else marked dead via refcount=0.
- `T_STR` payload encoding: payload values `< STRING_POOL_SLOTS` refer
  to the existing static string pool; payload values `>= STRING_POOL_SLOTS`
  are byte offsets into `s_heap`. To keep the discriminator
  unambiguous, the heap bump pointer is initialized to
  `STRING_POOL_SLOTS` so the first allocation returns an offset
  greater than any pool index.

Refcount management is **implicit in the VM stack ops** rather than
emitted as OP_RETAIN/OP_RELEASE by the compiler - see "Deviation from
OPCODES.md" below.

## Detailed design

### Heap module API (`src/runtime/heap.h`)

```c
typedef uint16_t heap_off_t;
#define HEAP_NULL ((heap_off_t)0xFFFF)

void heap_reset(void);                            /* clear; used by :reset */

heap_off_t heap_alloc(uint16_t payload_len);      /* HEAP_NULL on OOM */
unsigned char *heap_payload(heap_off_t off);      /* writable pointer */
uint16_t       heap_len(heap_off_t off);          /* payload bytes */

void heap_retain(heap_off_t off);                 /* ++refcount */
void heap_release(heap_off_t off);                /* --refcount; reclaim if 0 + topmost */

uint16_t heap_free_bytes(void);                   /* for :mem (Phase 5) */
```

### Header layout

```
offset  field         size
0,1     refcount      u16 little-endian
2,3     payload_len   u16 little-endian
4..     payload       payload_len bytes
```

### Value semantics for T_STR

| payload range                        | meaning                       |
|--------------------------------------|-------------------------------|
| `0 <= p < STRING_POOL_SLOTS`         | static pool index             |
| `STRING_POOL_SLOTS <= p < HEAP_SIZE` | heap byte offset (header at p)|

The compiler emits `OP_STR <pool_idx>` for compile-time literals that
fit in the pool (no escapes, no interpolation, short enough). For
larger or processed literals, it allocates on the heap at compile time
and emits `OP_STR <heap_off>` with the offset baked in.

The compiler therefore allocates on the heap *during compilation*, and
the heap persists across `compile_source` and `vm_run` - the existing
pattern for globals/string-pool. `heap_reset()` runs in file_runner
between programs (like `vm_reset_globals`); the REPL keeps the heap live
across input lines (same as it keeps globals live).

A compile-time-allocated string starts with refcount 1 owned by the
"compile-time constants" pool. Subsequent runtime references retain
on push, release on pop - see below.

### Implicit refcount in stack ops

When the VM pops, dups, defines/sets a global, etc., it inspects the
tag of the value being moved and adjusts refcounts:

- `OP_POP` of a heap-ref value → `heap_release(off)`
- `OP_DUP` of a heap-ref value → `heap_retain(off)` (the new copy)
- `OP_GET_GLOBAL` pushing a heap-ref value → `heap_retain(off)`
- `OP_DEFINE_GLOBAL` consuming TOS heap-ref → transfer (no retain, no release)
- `OP_SET_GLOBAL` replacing an existing heap-ref → release old, transfer new
- `OP_RETURN` / `OP_RETURN_V` (Phase 4) - release locals before returning
- `OP_CALL_BUILTIN` - each consumed arg, if heap-ref, gets released after the builtin returns

Pool strings have payload < `STRING_POOL_SLOTS`, which the VM checks
first; pool refs are not retained/released.

This **diverges from `OPCODES.md`** which calls for explicit
OP_RETAIN/OP_RELEASE emitted by the compiler. The compiler is single-pass
without a type system yet, so static reasoning about which slot holds
a heap ref is hard. Pushing the bookkeeping into the VM (which already
inspects the tag) costs one tag-check per stack op and removes a whole
class of bug. OP_RETAIN/OP_RELEASE remain reserved for future use
(e.g. across function boundaries in Phase 4+).

### Concatenation, interpolation

- `OP_STR_CONCAT` (a, b): allocates a new heap string of
  `len(a) + len(b)` bytes; payload is `a.bytes + b.bytes`; pushes
  the new heap ref. The VM is responsible for releasing the inputs
  via the implicit stack-op semantics above - the concat opcode pops
  both, performs the allocation, then pushes the result. The pops
  call release for heap refs.
- `OP_STR_INTERP_I`, `OP_STR_INTERP_B`, `OP_STR_INTERP_O`: convert
  TOS Int/Bool/Optional to a freshly-allocated heap string. When TOS is
  already a `T_STR`, `OP_STR_INTERP_I` passes it through unchanged, so
  `print("x = \(name)")` with a String `name` allocates no intermediate
  copy.

For an interpolated literal `"a = \(x), b = \(y)"`, the compiler
emits:

```
OP_STR "a = "             ; pool ref or heap ref
OP_GET_GLOBAL x
OP_STR_INTERP_I            ; x -> heap str "5"
OP_STR_CONCAT              ; "a = " + "5"
OP_STR ", b = "
OP_STR_CONCAT
OP_GET_GLOBAL y
OP_STR_INTERP_I
OP_STR_CONCAT
```

This is N-1 concats for N pieces. Phase 6 may add a dedicated
"concat N" opcode if profiling justifies it.

## Alternatives considered

1. **Tagged-pointer scheme** (set the high bit of the payload to mark
   heap vs. pool): rejected because it limits heap to 32 KB, which
   constrains us when we later want to point into HGR pages or the
   language card.
2. **Separate tags** `T_STR_POOL` and `T_STR_HEAP`: rejected because
   it doubles the cases in every value comparator and print path for
   marginal clarity gain.
3. **Explicit retain/release opcodes** as in `OPCODES.md`: documented
   above; deferred. The compiler will gain real type tracking in
   Phase 4 (function signatures), at which point we can revisit.
4. **Segregated free lists** as in `ROADMAP.md`: deferred. Phase 3
   programs (FizzBuzz, hello+name) churn through ≤ 100 string
   allocations of ≤ 16 bytes each. Pure bump + LIFO reclaim covers
   that. Segregation pays off when allocation patterns are mixed and
   the heap is contended - neither true in Phase 3.

## Cost

- **Memory cost**: 2 KB BSS (the heap) on both host and target.
  Optional `WITH_HEAP_SIZE=4096` Makefile override. Roughly 100 bytes
  of code for the allocator on cc65. Roughly 200 bytes of additional
  VM dispatch (the implicit refcount cases in OP_POP/DUP/etc.).
- **Performance cost**: one tag-check per stack op for refcount
  bookkeeping. Pool-resident strings short-circuit immediately.
- **Code complexity**: small. The allocator is < 100 lines; the VM
  changes are localized to the few stack ops that need refcount
  awareness.
- **Schedule cost**: ~half a day for allocator + tests; another half
  for the VM/compiler integration.

## Migration

- `STRING_POOL_SLOTS` rises from 4 → 16 to match the Phase 4 limit
  in `LANGUAGE.md`. The existing pool-resident hello-world entry
  stays at index 0.
- The VM gains heap-awareness on the existing OP_POP/OP_DUP/OP_*GLOBAL
  paths. Existing bytecode (which never uses heap-ref strings) is
  unaffected.
- `heap_reset()` is added to the file_runner's pre-compile sequence
  alongside `globals_reset()` / `vm_reset_globals()`.

Implicit refcount in the stack ops is ASAN-clean on the host with no
observable leaks in the test programs. Segregated free lists in
`heap_alloc` (for when fragmentation matters) and compiler-emitted
`OP_RETAIN` / `OP_RELEASE` at function boundaries remain reserved for a
later phase; pure bump + LIFO reclaim covers the current allocation
patterns.
