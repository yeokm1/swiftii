# 009 - Compile-time type tracker

This doc describes the type tracker that shipped. The earlier
`OP_REPL_ECHO` extension and REPL-echo plumbing were abandoned with the
REPL polish layer; argument-label validation was deferred.

## Problem

Phase 3–4 shipped the surface syntax for SwiftII's type system but
none of the static checking that gives it meaning:

- `let x: Int = "hi"` compiles. `let m: Int? = "x"` compiles. The
  annotation is consumed and discarded.
- `func add(a: Int, b: Int) -> Int` accepts `add("x", true)` because
  argument labels and argument types are not validated.
- `[Type]` annotation form (`var xs: [Int] = []`) is rejected by the
  parser - the colon-then-type branch only accepts a bare `IDENT`
  optionally followed by `?`.
- `[1, "two"]` reaches the VM as a mixed-tag array; the homogeneity
  rule from `LANGUAGE.md section Arrays` is enforced "by hoping the runtime
  notices."
- `if let x = opt` works but does not record that `x` has the inner
  type of `opt`.

Phase 7's remaining Tier 2 features need static type information to
do their jobs: `switch` pattern arms, `struct` fields, `xs[i] = v`
homogeneity, signature-overloaded conversion builtins. The type
tracker is the single piece of compiler infrastructure that unblocks
them. It lands first; the rest of Phase 7 builds on it.

`AGENTS.md` requires a design doc for changes that touch the memory
map or the bytecode format. The original proposal extended
`OP_REPL_ECHO` with a `ctype` operand; that extension was dropped
when the REPL polish layer (and `OP_REPL_ECHO` itself) was removed
2026-05-23, so the *shipped* tracker does not modify the bytecode
format - it is purely a compile-time addition.

## Proposal (as shipped)

Add a 1-byte compile-time type code (`ctype_t`) tracked alongside
every named binding (globals, locals, function parameters and
returns) and propagated through expression compilation via a
**single-slot** type register (not the full stack the original
proposal sketched - see "Deviation" below). Compile-time type errors
short-circuit the compile with a single shared `"type mismatch"`
error string.

The tracker only runs at compile time. The VM continues to dispatch
on runtime tags only - no opcode adds a type check at runtime, and
no opcode gains a type operand. The REPL no longer echoes
`name: Type = value` at all, so the originally-planned ctype
operand on `OP_REPL_ECHO` is moot.

Implementation landed as Phase 7 commits 2 → 6 across 2026-05-23 / 24.

## Detailed design

### Compile-time type code (`ctype_t`)

A single byte encodes every type SwiftII supports today. Defined in
`src/common/ctype.h`:

```c
typedef unsigned char ctype_t;

#define CT_UNKNOWN  0x00  /* not yet determined / opaque fallback   */
#define CT_VOID     0x01  /* () return                              */
#define CT_INT      0x02  /* Int                                    */
#define CT_BOOL     0x03  /* Bool                                   */
#define CT_STRING   0x04  /* String                                 */
/* Optionals: high bit set, low nibble = inner type code            */
#define CT_OPT_BIT  0x80
#define CT_OPT_INT     (CT_OPT_BIT | CT_INT)     /* Int?            */
...
/* Arrays: 0x40 set, low nibble = element type code                 */
#define CT_ARR_BIT  0x40
#define CT_ARR_INT     (CT_ARR_BIT | CT_INT)     /* [Int]           */
...
#define CT_ARR_UNKNOWN (CT_ARR_BIT | CT_UNKNOWN) /* empty literal   */
#define CT_NIL_LIT  0x10  /* bare `nil` - unifies with any T?       */
```

`CT_ARR_UNKNOWN` (not in the original proposal) is the ctype an
empty array literal carries before its element type is constrained
by the surrounding context.

Optional-of-array (`[Int]?`) and array-of-optional (`[Int?]`) are
**not** representable; `parse_type` rejects them.

`ctype_describe(ct)` returns the printable name (`"Int"`, `"[Int]"`,
`"Int?"`, ...) for error context and `:list`. The table sits in LC
RODATA so it doesn't pressure main-RAM RODATA.

### Symbol table additions

Each existing table gained a ctype column, stored in parallel arrays
beside the entry struct (not inside it - cc65 emits struct-stride
bloat for any non-power-of-2 stride; see the Phase 7 commit 2
deviation):

- `globals.c` - `s_ctypes[i]` (32 entries × 1 B = 32 B BSS).
- `locals.c` - `s_local_ctypes[i]` (16 × 1 B = 16 B BSS).
- `funcs.c` - `s_ret_ctype[fn]` + `s_param_ctypes[fn][6]` (16 funcs
  × 7 B = 112 B BSS).

API getters/setters live alongside the existing name/index accessors.
Total new BSS: ~160 B.

### Single-slot expression-type register

The original proposal was an 8-slot type stack mirroring the VM's
value stack. Phase 7 commit 4b shipped a **single-slot register**
instead (`static ctype_t s_expr_ctype` in `src/compiler/types.c`)
saving ~200 B of code. The register holds the ctype of the most
recently emitted expression; Pratt parselets update it as they emit
opcodes:

```c
void comp_set_expr_ctype(ctype_t ct);
ctype_t comp_get_expr_ctype(void);
```

This is enough for every Phase 7 use case because validation happens
at well-defined points (assignment target, function argument site,
subscript index, condition expression) - none of them need to recall
the type of an expression two pushes ago. If a future feature does
need a real stack, the API is forward-compatible.

The mapping from emit site to type update is the same shape as the
original table, just keyed off the register rather than a stack:
`OP_INT_*` sets `CT_INT`, `OP_STR_LIT` sets `CT_STRING`,
`OP_GET_GLOBAL n` sets `globals_get_ctype(n)`, force-unwrap strips
`CT_OPT_BIT`, subscript yields the array's element type, etc.

### Unification rule

`ctype_unifies` (in `src/compiler/types.c`) is more permissive than
the original proposal - `CT_UNKNOWN` unifies with anything in both
directions. This keeps legacy code paths the tracker can't yet pin
(mixed-type binary ops, opaque-type annotations) compiling; strict
checks are applied at the specific validation sites that need them.

```c
int ctype_unifies(ctype_t expected, ctype_t actual) {
  if (expected == actual) return 1;
  if (expected == CT_UNKNOWN || actual == CT_UNKNOWN) return 1;
  if ((expected & CT_OPT_BIT) != 0) {
    if ((expected & ~CT_OPT_BIT) == actual) return 1;
    if (actual == CT_NIL_LIT) return 1;
  }
  if (expected == CT_ARR_UNKNOWN && (actual & CT_ARR_BIT) != 0) return 1;
  if (actual == CT_ARR_UNKNOWN && (expected & CT_ARR_BIT) != 0) return 1;
  return 0;
}
```

Note `T ↔ T?` is one-way: passing an `Int?` to an `Int` parameter is
a compile error (must unwrap with `!` or `??` first). This matches
Swift exactly.

### Validation helpers

Three helpers in `src/compiler/types.c` host the validation pattern
shared by every call site:

- `resolve_decl_ctype(p, declared, inferred)` - used by `let` / `var`
  declarations. If `declared == CT_UNKNOWN` the inferred type wins;
  else the inferred type must unify with `declared`, otherwise fail.
- `check_type_match(p, expected)` - used wherever an expression's
  ctype must match an expected ctype (call-arg sites, assignment
  RHS, subscript-set value).
- `check_and_emit_set(p, target, op, idx)` - wraps the assignment
  check + emit of `OP_SET_GLOBAL` / `OP_SET_LOCAL`. Three LC call
  sites share this helper, which is the reason it lives in main CODE
  (LC-to-main jumps are cheap; the alternative was duplicating the
  check+emit at each site, which blew the LC budget).

### `[T]` annotation parsing

`parse_type` (Phase 7 commit 3) replaces the inline IDENT+QUESTION
parser at three call sites (`parse_var_decl`, `parse_func`
parameters, `parse_func` return type). Grammar:

```
type   := IDENT ('?')?           // Int, Int?
        | '[' IDENT ']'           // [Int]
```

`[Int?]` and `[Int]?` are rejected at the parser level
(`"unsupported type"`) per the encoding constraint above.

`base_type_from_span` (Phase 7 commit 4a) maps the IDENT span to
`CT_INT` / `CT_BOOL` / `CT_STRING`; unknown
identifiers stay `CT_UNKNOWN` (the parser still accepts the
annotation; the tracker treats it as opaque).

### Empty array literal

`var xs = []` remains a compile error (no annotation to constrain
the element type). `var xs: [Int] = []` compiles: `parse_type` yields
`CT_ARR_INT`, the empty literal pushes `CT_ARR_UNKNOWN`, and the
unification rule above accepts the pair.

### `if let` and inner type

`if let x = opt { ... }` reads `opt`'s ctype (say `CT_OPT_INT`),
strips the `CT_OPT_BIT`, and declares `x` as a local with `ctype =
CT_INT`. The Phase 7 expansion of `if let` to function bodies
inherits this without further work.

### Error messages

A single shared error string covers all type-mismatch faults:

```c
const char ERR_TYPE_MISMATCH[] = "type mismatch";
```

Per-fault context (`"expected Int, got String"`) was costed at ~360 B
RODATA and rejected on size grounds. The single message is paired
with the existing token-position printout; the user can read the
offending expression.

### What did **not** ship

- **`OP_REPL_ECHO` extension.** The original proposal extended the
  opcode with a `ctype` operand byte so the REPL could print
  `a: [Int] = [1, 2, 3]`. The REPL polish layer (and `OP_REPL_ECHO`
  itself) was removed 2026-05-23 - see design doc 005 and ROADMAP
  "Maybe / probably never" item 26. The REPL no longer echoes
  bindings at all; `:list` uses `builtins_type_name` (runtime tag,
  shows `"Array"`) rather than `ctype_describe` (shows `"[Int]"`).
  If REPL echoes ever come back, they will need to thread ctypes
  through whatever new echo mechanism is designed; the engine side
  is ready.
- **Argument-label validation.** Deferred at Phase 7 commit 6
  (2026-05-24) - needs new per-param label storage and ~50 B of LC
  the c6 close did not have. The Phase 4 limitation logged 2026-05-20
  remains open.
- **8-slot type stack.** Single-slot register shipped instead;
  see "Deviation" above.
- **Per-fault error messages.** Single `ERR_TYPE_MISMATCH` string.

## Engineering log (Phase 7 sub-commits)

```
commit 1: this design doc.                                    no code
commit 2: ctype_t enum + symbol-table plumbing only           +391 B file
          (parallel s_ctypes / s_ret_ctypes / s_param_ctypes  +162 B BSS
          arrays kept *outside* GlobalEntry/FnEntry to        landed
          preserve cc65's power-of-2 struct stride;           2026-05-23
          API gains ctype arg + getter/setter; statements.c
          callsites pass CT_UNKNOWN).
commit 3: parse_type wrapper - refactors the three inline      −41 B file
          IDENT+QUESTION annotation parsers into one helper;   landed
          base-type recognition + [T] form deferred (didn't    2026-05-23
          fit). Every annotation still stores CT_UNKNOWN.
budget sweep: cut :raw + Tier-3 keywords + hex/bin/oct lits +  −836 B
          underscore digit separators.                         landed
                                                               2026-05-23
commit 4a: base_type_from_span + parse_type [T] form +        +784 B
           builtins_write_ctype + :list uses globals_get_ctype landed
           (no OP_REPL_ECHO change - that opcode no longer     2026-05-23
           exists). Annotated bindings now have queryable
           declared types ([Int] / Int? / String?).
help trim: compress :help //+ block to one line.              −327 B
                                                              landed
                                                              2026-05-23
commit 4b: Pratt expression-type tracking. Single-slot        +320 B
           "last expression ctype" register (in types.c)       landed
           updated by each Pratt parselet. Force-unwrap        2026-05-23
           strips `?`; subscript yields elem type. Skipped a
           full type stack - single-slot covers the demo
           cases with much less code (~320 B vs ~500+ B for a
           stack).
commit 5:  validation: let/var annotation check, assignment   +298 B
           check, ctype_unifies + resolve_decl_ctype +         landed
           check_and_emit_set helpers in types.c.              2026-05-23
           `let x: Int = "s"` now errors; `var n: Int = 1;
           n = "s"` errors at assignment. Call-arg type check
           and argument-label validation deferred - the per-
           arg hook didn't fit the remaining LC ceiling.
           Tests/repl/014_type_validation locks in the
           working cases.
commit 6: ship deferred Phase 4 items that depend on the
          tracker - subscript-set, xs.isEmpty + deferred c5      +326 B
          call-arg type check. Arg-label check still             landed
          deferred (needs new per-param label storage and        2026-05-24
          ~50 B LC the c6 close did not have). Funded by the
          REPL-polish reclaim (78f0209), not Phase 6b. New
          `check_type_match` helper in types.c (main CODE)
          hosts the ctype_unifies + parser_fail combo that
          three LC call sites now share.
```

**Commit 2 deviation (2026-05-23):** the original plan put
`parse_type` + the `[T]` form in commit 2, but the first build of
that scope blew the binary budget by 493 B - `parse_type` plus
`base_type_from_span` in LC (`statements.c` lives in LC) cost ~280 B
and the cc65 struct-stride bloat from adding a single byte to
`GlobalEntry` / `LocalEntry` / `FnEntry` cost another ~380 B in
main CODE. Two interventions recovered most of it: (a) split ctype
storage into parallel `s_ctypes[]` arrays sitting beside the entry
struct so the structs keep their power-of-2 stride (gained back
~221 B of main CODE), and (b) move `parse_type` to commit 3.

**Commit 3 further trim (2026-05-23):** the original commit 3 scope
of `parse_type` + `base_type_from_span` + `[T]` form + Pratt
propagation + (then) OP_REPL_ECHO extension was measured at +482 B
file. The shipped commit 3 trims further: parse_type wraps the
existing IDENT+QUESTION parse into one helper (so the three callsites
read annotations uniformly) and defers base recognition + `[T]` +
Pratt propagation into a combined **commit 4**. Commit 3 actually
shrinks the binary by 41 B because three inline parse blocks fold
into one call.

**REPL polish removal (2026-05-23, commit 78f0209):** the
`OP_REPL_ECHO` extension this doc originally proposed never landed
because the opcode itself was removed alongside numbered prompts and
`$R<n>` to fund Phase 7 commit 6's deferred work. See design doc 005.

## Alternatives considered

(Preserved from the original proposal for context; the picks that
actually shipped are marked.)

**Carry types through Pratt return values.** Rejected (~70 callsites
to thread).

**Two-pass compiler.** Rejected (doubles compile time; needs an AST
the BSS budget can't afford).

**Two-byte ctype.** Rejected (doubles BSS; nested optionals/arrays
not in any Phase 7 demo). Two-byte extension is forward-compatible.

**Per-statement type stack (original plan).** Costed at ~500 B CODE;
the single-slot register that shipped costs ~320 B and covers every
Phase 7 use case. **The single-slot register won.**

**Per-fault error messages.** Rejected (~360 B RODATA). Single
`ERR_TYPE_MISMATCH` shipped.

**Skip the REPL echo upgrade.** Marked "rejected" in the original
proposal - but became moot when the entire REPL echo path was
removed. **The minimal `:list` path that shipped is the closest live
equivalent.**

## Cost (actual at Phase 7 commits 2–6 close)

Net binary delta across commits 2 → 6: +1,532 B file (the +391 −41
+784 +320 +298 +326 numbers above sum to +2,078 B; the −836 B budget
sweep and −327 B help trim landed between them, leaving a net of
+915 B file across the type-tracker-attributable commits, or +1,532 B
including dependents).

BSS additions: ~160 B (32 globals + 16 locals + 112 funcs).

Performance cost: zero at runtime (no new opcode dispatch, no
runtime type check). Compile time grows by ~10–15%; below the REPL
latency floor.

## Migration

- **Bytecode format unchanged.** The original proposal added an
  operand to `OP_REPL_ECHO`; that opcode no longer exists, so the
  shipped tracker leaves the on-disk-equivalent bytecode format
  bit-identical to Phase 6.
- **Existing tests** that exercised previously-accepted type
  mismatches were audited at commit 5; each break was either a wrong
  test (fixed) or a tracker bug (fixed).
- **Phase 4 demo programs** type-check cleanly. No demo program in
  `tests/integration/` broke.
- **One-way doors:** the ctype encoding still cannot represent
  `[Int?]` or `[Int]?`. Forward-compatible two-byte extension
  documented under "Alternatives considered."

## Decision

Implemented with the deviations recorded above. The `OP_REPL_ECHO`
extension was abandoned with the rest of the REPL polish layer.
