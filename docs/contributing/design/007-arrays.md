# 007 - Array memory layout and operations

## Problem

Phase 4 introduces arrays as a Tier 1 feature (per
`docs/using/LANGUAGE.md section Arrays` and `ROADMAP.md section Phase 4`). The Phase 3
heap allocator (`docs/contributing/design/002`) and the `T_ARR` tag in
`src/common/types.h` are in place, but there is no defined heap
representation for an array, no append-growth policy, no element
refcount discipline, no bounds-check semantics, and no decision about
empty-literal vs. typed-empty syntax. Implementing any of
`OP_NEW_ARRAY` / `OP_ARR_GET` / `OP_ARR_SET` / `OP_ARR_LEN` /
`OP_ARR_APPEND` without first nailing these down would lock the
project into accidental choices. AGENTS.md explicitly calls for a
design doc on "anything that touches the memory map or the bytecode
format" - array layout qualifies on both counts.

## Proposal

Heap-allocate every array as a single contiguous block. The block uses
the standard Phase 3 heap header (`refcount u16`, `payload_len u16`)
followed by an array-specific payload:

```
payload[0..1]   count    u16  live element count
payload[2..3]   capacity u16  number of slots allocated
payload[4..]    slots    capacity × 3 bytes (tagged Value per slot)
```

`payload_len` in the heap header equals `4 + capacity * 3` (the total
allocated payload size, not the live count). The `T_ARR` Value
payload is the heap offset of the header, exactly as `T_STR`'s heap
case works.

Append grows the array by allocating a new block with a larger
capacity (initial capacity → 4 for an empty literal; doubling on
overflow), copying live slots over, retaining each heap-bearing
element, then releasing the old block. The compiler emits
`OP_ARR_APPEND` regardless of in-place feasibility - the VM decides
whether to grow.

Element ownership is tracked by **the array**. When the array's own
refcount hits zero, the VM iterates live slots and calls
`value_release` on each, then `heap_release` on the block. When a
slot is overwritten via subscript-set, the VM releases the old slot
value and retains the new one.

Arrays are homogeneous (per `LANGUAGE.md`). The compiler enforces
homogeneity at the language level - no element-type tag is stored in
the heap.

## Detailed design

### Heap payload

```
offset  field      size  notes
0,1     count      u16   little-endian, current live elements
2,3     capacity   u16   little-endian, slot capacity (>= count)
4..     slot[0]    3 B   tagged Value (tag, lo, hi)
7..     slot[1]    3 B   ...
...
4 + 3*capacity - 1     end of payload
```

The header's `payload_len` is `4 + 3 * capacity`. The bump allocator
sees the whole block; only the array helpers know the count/capacity
split.

### New runtime API (`src/runtime/array.h` / `array.c`)

```c
heap_off_t array_new(uint16_t initial_capacity);     /* HEAP_NULL on OOM */
uint16_t   array_count(heap_off_t arr);              /* live count */
uint16_t   array_capacity(heap_off_t arr);           /* slot capacity */
int        array_get(heap_off_t arr, uint16_t i, Value *out); /* 0=ok, 1=OOB */
int        array_set(heap_off_t arr, uint16_t i, const Value *v); /* 0=ok, 1=OOB; releases old, retains new */
int        array_append(heap_off_t *arr_inout, const Value *v); /* 0=ok, 1=OOM; may reallocate */
void       array_release_elements(heap_off_t arr);   /* called by value_release when arr refcount hits 0 */
```

`array_append` takes `heap_off_t *` because it may reallocate. The VM
update path is: pop array Value, call `array_append` (which may
mutate the offset), push the same Value with the (possibly new)
payload. For appends that fit within capacity, no allocation
happens; only `count` advances.

Growth policy: `new_capacity = max(4, old_capacity * 2)`. The doubling
guarantees amortised O(1) append at the cost of leaving the old block
behind (it becomes dead heap bytes unless it was topmost; LIFO reclaim
catches the topmost case as usual). For Phase 4 demos building arrays
of ≤50 elements this leaks roughly log₂(50) × (old block size)
≈ 1 KB worst case in a 2 KB heap - acceptable for v1; revisit in
Phase 5 if real programs hit the ceiling.

### New opcode dispatch (`src/vm/vm.c`)

| Opcode          | Pops                | Pushes      | Effect                                            |
|-----------------|---------------------|-------------|---------------------------------------------------|
| `OP_NEW_ARRAY n`| `n` × Value         | `T_ARR`     | alloc capacity=n, copy slots, retain heap elems   |
| `OP_ARR_GET`    | `T_ARR`, `T_INT i`  | element     | bounds-check; retain heap element if needed       |
| `OP_ARR_SET`    | `T_ARR`, `T_INT i`, `v` | -       | bounds-check; release old, retain new             |
| `OP_ARR_LEN`    | `T_ARR`             | `T_INT`     | reads count                                       |
| `OP_ARR_APPEND` | `T_ARR`, `v`        | -           | grow if needed; retain v; updates count           |

Bounds violation → `SE_RUNTIME` with message `"array index out of
bounds"`. Negative index → same (the value is signed int16; treat as
unsigned for the comparison after a `< 0` check).

`OP_ARR_APPEND` is statement-shaped: it consumes the array reference
and discards. Method-call syntax `xs.append(v)` therefore desugars to
`OP_GET_GLOBAL xs / push v / OP_ARR_APPEND` followed by an `OP_POP`
of the (no-op) result - actually `OP_ARR_APPEND` returns nothing, so
no trailing pop is needed. The compiler treats it as a void
statement.

Method-property syntax `xs.count` and `xs.isEmpty` compile to
`OP_GET_GLOBAL xs / OP_ARR_LEN` (then `OP_INT_U8 0 / OP_EQ` for
`.isEmpty`).

### Compiler integration

- Pratt primary: `[` opens array-literal expression. Parse
  comma-separated expressions (≥ 1 element), expect `]`, emit
  `OP_NEW_ARRAY n`. Type-check element homogeneity in a follow-up
  pass once the type checker exists; for Phase 4 the lexer-level
  enforcement is "if you mix tags the runtime will be unhappy" -
  acceptable since user programs that pass through host tests catch
  it.
- Pratt postfix: after parsing a primary expression, if the next
  token is `[`, parse `index_expr`, expect `]`. If an assignment
  operator follows, compile `OP_ARR_SET`; otherwise `OP_ARR_GET`.
- Pratt postfix: after parsing a primary, if next is `.`, peek the
  identifier. If it is `count` → `OP_ARR_LEN`; if `isEmpty` →
  `OP_ARR_LEN`, `OP_INT_U8 0`, `OP_EQ`; if `append` → consume `(`,
  parse argument, consume `)`, emit `OP_ARR_APPEND`. Other
  identifiers fall through to "unknown member" error.
- Type annotation: `[T]` in a `let`/`var` annotation parses to "array
  of T" but is only validated, not stored, in Phase 4 (single-pass
  compiler doesn't carry per-variable element types yet).
- Empty literal: `[]` is a syntax error in expression position. To
  initialise an empty array, require a typed annotation:
  `var xs: [Int] = []` - the compiler then emits `OP_NEW_ARRAY 0`.
  This avoids type-inference ambiguity in single-pass compilation.

### Print of arrays

`builtins_print_value` learns a `T_ARR` case that prints
`[elem0, elem1, ...]` by iterating the array and recursively printing
each slot. To avoid unbounded recursion on nested arrays (which Phase 4
permits via `[[1,2],[3,4]]`), the recursion is bounded by a static
depth counter (max 2); deeper nesting prints as `[...]`. This
matches the demo audience's needs and keeps stack usage bounded
under cc65's 4-deep recursion rule from `CONSTRAINTS.md`.

### Value retain/release update

`value_retain`/`value_release` (`src/runtime/value.c`) gain a
`T_ARR` branch that calls `heap_retain`/`heap_release` on the array
offset. When `heap_release` drops the refcount to zero, the heap
layer notices the tag (carried in the value, not the header) and
calls `array_release_elements` *before* reclaiming bytes. Because
the heap header is tag-agnostic, the call is initiated from
`value_release` (which has the tag in hand), not from
`heap_release`. Concretely:

```c
void value_release(const Value *v) {
    if (v->tag == T_STR && is_heap_str(v)) {
        heap_release(payload_off);
    } else if (v->tag == T_ARR) {
        heap_off_t off = (heap_off_t)VALUE_PAYLOAD_U16(*v);
        /* Peek refcount; if dropping to zero, release elements first. */
        if (heap_refcount(off) == 1) {
            array_release_elements(off);
        }
        heap_release(off);
    }
}
```

A new `heap_refcount(off)` accessor is added (read-only peek). It is
the only addition to the heap API; no header layout change.

### Edits to existing docs

- `OPCODES.md`: existing rows for $85–$89 already describe these
  opcodes at one-liner level; no change beyond marking them
  "implemented in Phase 4."
- `MEMORY_MAP.md section Heap layout`: add a "Payload formats" subsection
  noting "Array (Phase 4): u16 count, u16 capacity, capacity × 3-byte
  Value slots."
- `LANGUAGE.md section Arrays`: add the "empty literal requires typed
  annotation" rule and the bounds-check error message text.
- `LESSONS.md`: append a paragraph after Phase 4 closes about
  realised array growth costs in the 2 KB heap, if non-obvious.

## Alternatives considered

**In-place growth with capacity stored in the header.** The Phase 3
heap header is fixed at `(refcount, payload_len)`. Allowing arrays
to grow in place would require either an alternate header layout
(tag in the header - couples heap to value system) or per-block
metadata stored elsewhere (complicates the bump allocator's reclaim).
Rejected: the doubling-and-reallocate scheme keeps the heap
allocator dumb.

**Linked-list array.** Cons cells, append by chasing. Avoids
reallocation but blows the 6-byte-per-element budget to 9+ bytes
per cons (refcount, next ptr, value). Rejected on size grounds - a
50-element array goes from 304 bytes (header + 50×3 slot) to ~750
bytes.

**Element-type tag in the heap payload.** Cheap (1 byte) but adds
nothing for Phase 4 since the compiler enforces homogeneity
upstream and the VM doesn't dispatch on element type. Rejected as
premature.

**Empty literal `[]` with type inference deferred to use site.**
Single-pass compilation cannot pin the element type retroactively
when the first `append` happens - the `OP_NEW_ARRAY 0` would emit
before any element is seen. Rejected: requires multi-pass typing
or a runtime "untyped empty" state, both heavyweight. Mandating
`[T]` annotation on empty initialisers is a small ergonomic cost
that the demo audience won't hit (the natural `var xs = [first]`
pattern just works).

**Pre-allocate a fixed-size capacity (no growth).** Caps array size
at compile time; would simplify the allocator. Rejected because
demos that read inputs of unknown length need growable arrays -
exactly the "reading numbers until EOF" case in the Phase 4 roadmap
demo program.

## Cost

**Memory cost (CODE):** ~300 bytes for `array.c` (allocation, get,
set, append, release_elements) + ~50 bytes for the print path +
~80 bytes for the five VM opcode cases + ~120 bytes for the Pratt
postfix rules for subscript and dot-member.  Total: ~550 bytes
CODE. Phase 4 is expected to exceed the 28 KB CODE budget regardless
(per `docs/contributing/design/004` section Cost); this is one of the budget items the
Phase 6 optimisation pass will reckon with.

**Memory cost (BSS):** zero static. All array storage is heap-resident.

**Memory cost (heap, worst-case demo):** a 50-element Int array
through doubling reallocations leaks ~1 KB of dead heap bytes in
addition to the 304 bytes of live payload. Total heap footprint
~1.3 KB out of 2 KB. Demos that build a single growing array are
fine; demos that build multiple growing arrays will OOM. Acceptable
for v1; Phase 5 may revisit if needed.

**Performance cost:** `OP_ARR_GET` / `OP_ARR_SET` are two heap reads
plus a bounds check - comparable to `OP_GET_GLOBAL` plus indexing.
`OP_ARR_APPEND` in the no-growth fast path is one capacity check +
slot write + count increment. The slow path (growth) copies all
existing slots; amortised O(1) per append.

**Code complexity cost:** Low-to-medium. The reallocation discipline
on `array_append` is the most subtle piece; the rest is mechanical.
Adds one new heap-layer function (`heap_refcount`) - a read-only
peek, no allocator change.

**Schedule cost:** Estimated ~1.5 days for full implementation +
tests (host + sim + integration + REPL).

## Migration

No existing tests or programs break. Arrays are new in Phase 4;
nothing references the `T_ARR` tag in compiled bytecode today
(verified by grep at design-doc time). The `heap_refcount` accessor
is a pure addition to the heap public API.

## Decision

Implemented during Phase 4 commit 5 (arrays), after functions,
optionals, break, and the print-terminator overload landed.
