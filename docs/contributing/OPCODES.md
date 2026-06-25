# OPCODES.md

The SwiftII bytecode instruction set. This document is the single
source of truth - the C enum (`src/vm/opcodes.h`), the VM dispatch
table, and the disassembler are hand-maintained and kept in sync with
it.

If you need a new opcode, add it here first, get a thumbs-up, then
implement.

---

## Format

Each instruction is **1 byte of opcode** followed by **0–2 bytes of
inline operand**. The VM is stack-based: most opcodes consume operands
from the top of the stack and push a result.

Operand encodings:

- `u8`  - unsigned 8-bit literal byte
- `u16` - unsigned 16-bit, little-endian (two bytes)
- `i16` - signed 16-bit, little-endian, used for jump offsets

There are no multi-byte opcodes and no opcode prefixes. We have ≤ 256
opcodes; `$FF` is reserved as the trap/illegal marker (see the
expansion-range list near the end of this file).

Any change to the opcode set, the builtin id assignments, or the
`.swb` funcs-record layout must also **bump `SWB_VERSION`** in
`src/swb/swb.h` - a stale Family B Runner then rejects a newer `.swb`
up front ("bad .swb image") instead of dying mid-program on an opcode
it doesn't know.

The "Stack" column uses Forth-style notation: `( before -- after )`
shows what the opcode pops (left) and pushes (right). Top of stack is
on the right.

---

## Opcode summary

Opcodes are grouped logically. Their numeric values are *stable* once
shipped; new opcodes get the next free number in their group, or in
the "expansion" range at the end.

### Constants and immediates ($00-$0F)

| Hex | Name        | Operand | Stack            | Notes                        |
|-----|-------------|---------|------------------|------------------------------|
| $00 | OP_NIL      | -       | ( -- nil )       | push T_NIL                   |
| $01 | OP_TRUE     | -       | ( -- true )      |                              |
| $02 | OP_FALSE    | -       | ( -- false )     |                              |
| $03 | OP_INT_U8   | u8      | ( -- int )       | push small int (0-255)       |
| $04 | OP_INT_I16  | i16     | ( -- int )       | push 16-bit signed int       |
| $05 | OP_CONST    | u8      | ( -- value )     | reserved; not emitted / no VM case today |
| $06 | OP_STR      | u16     | ( -- str )       | push string from string pool |
| $07 | OP_OPT_SOME | -       | ( v -- some(v) ) | reserved; not emitted / no VM case today |
| $08 | OP_OPT_NONE | -       | ( -- none )      | reserved; not emitted / no VM case today |

The constant pool is for values that don't fit in inline operands or
that recur frequently. Strings are interned in a separate string pool
(strings are heap-allocated and accessed by stable index).

### Stack manipulation ($10-$1F)

| Hex | Name      | Operand | Stack          | Notes                         |
|-----|-----------|---------|----------------|-------------------------------|
| $10 | OP_POP    | -       | ( v -- )       |                               |
| $11 | OP_DUP    | -       | ( v -- v v )   |                               |
| $12 | OP_SWAP   | -       | ( a b -- b a ) |                               |
| $13 | OP_OVER   | -       | ( a b -- a b a)|                               |
| $14 | OP_POP_N  | u8      | ( v1...vN -- ) | reserved; not emitted / no VM case today |

### Variables ($20-$2F)

| Hex | Name              | Operand | Stack       | Notes                |
|-----|-------------------|---------|-------------|----------------------|
| $20 | OP_GET_GLOBAL     | u8      | ( -- v )    | global index in u8   |
| $21 | OP_SET_GLOBAL     | u8      | ( v -- )    | assign to global     |
| $22 | OP_DEFINE_GLOBAL  | u8      | ( v -- )    | first-time bind      |
| $23 | OP_GET_LOCAL      | u8      | ( -- v )    | offset from frame ptr; reads `s_stack[fp + u8]` |
| $24 | OP_SET_LOCAL      | u8      | ( v -- )    | writes `s_stack[fp + u8]`, releases old value |

Global indices are 8-bit. The current caps - `MAX_GLOBALS` 32 in Family A,
48 in Family B - stay well under 256, so a wide variant would never fire. If a
future tier raises the global cap past 256, allocate `OP_GET_GLOBAL_W` /
`OP_SET_GLOBAL_W` at $25/$26 then.

### Arithmetic ($30-$3F)

All arithmetic operates on `T_INT`. Mixed types are a runtime error.

| Hex | Name      | Operand | Stack            | Notes                  |
|-----|-----------|---------|------------------|------------------------|
| $30 | OP_ADD    | -       | ( a b -- a+b )   | int overflow wraps     |
| $31 | OP_SUB    | -       | ( a b -- a-b )   |                        |
| $32 | OP_MUL    | -       | ( a b -- a*b )   | software multiply      |
| $33 | OP_DIV    | -       | ( a b -- a/b )   | runtime err if b==0    |
| $34 | OP_MOD    | -       | ( a b -- a%b )   | runtime err if b==0    |
| $35 | OP_NEG    | -       | ( a -- -a )      |                        |
| $36 | OP_INC    | -       | ( a -- a+1 )     | optimization for loops |
| $37 | OP_DEC    | -       | ( a -- a-1 )     | reserved; not emitted / no VM case today |

`OP_INC` exists because `for-in` over ranges produces it constantly and
the savings on dispatch overhead are real. `OP_DEC` keeps its slot for a
future descending-loop/codegen use, but currently falls through to
`SE_BAD_OPCODE` if executed.

### Comparison and logic ($40-$4F)

| Hex | Name     | Operand | Stack             | Notes                  |
|-----|----------|---------|-------------------|------------------------|
| $40 | OP_EQ    | -       | ( a b -- bool )   | works on Int, Bool, nil|
| $41 | OP_NEQ   | -       | ( a b -- bool )   |                        |
| $42 | OP_LT    | -       | ( a b -- bool )   | Int only               |
| $43 | OP_LE    | -       | ( a b -- bool )   | Int only               |
| $44 | OP_GT    | -       | ( a b -- bool )   | Int only               |
| $45 | OP_GE    | -       | ( a b -- bool )   | Int only               |
| $46 | OP_NOT   | -       | ( bool -- bool )  |                        |
| $47 | OP_STR_EQ| -       | ( s1 s2 -- bool ) | reserved; not emitted  |

`OP_STR_EQ` is a historical reservation, not shipped VM behavior. Today string
`==` / `!=` compile to `OP_EQ` / `OP_NEQ` and compare object identity, matching
the language reference.

Logical `&&` and `||` are implemented at the compiler level via
short-circuit jumps, not as bytecode opcodes.

### Control flow ($50-$5F)

| Hex | Name             | Operand | Stack       | Notes                  |
|-----|------------------|---------|-------------|------------------------|
| $50 | OP_JUMP          | i16     | ( -- )      | unconditional, signed  |
| $51 | OP_JUMP_IF_FALSE | i16     | ( bool -- ) | pops, branches if false|
| $52 | OP_JUMP_IF_TRUE  | i16     | ( bool -- ) | pops, branches if true |
| $53 | OP_LOOP          | u16     | ( -- )      | backward jump (unsigned)|
| $54 | OP_HALT          | -       | ( -- )      | end of top-level prog  |

`OP_LOOP` exists because backward jumps are common (`while`, `for-in`)
and a separate opcode lets us use unsigned offsets, doubling the
practical loop-body size.

### Function calls ($60-$6F)

| Hex | Name        | Operand    | Stack                        | Notes                  |
|-----|-------------|------------|------------------------------|------------------------|
| $60 | OP_CALL     | u8 + u8    | ( arg1...argN -- result )    | fn_idx, argc           |
| $61 | OP_RETURN   | -          | ( v -- result-on-caller )    | from a value-returning function |
| $62 | OP_RETURN_V | -          | ( -- nil-on-caller )         | void return → nil      |
| $63 | OP_CALL_BUILTIN | u8 + u8 | ( arg1...argN -- result )   | id, argc               |

`OP_CALL` uses a Tier 1 design: the function is not a first-class value.
Both operand bytes are inline: `fn_idx` is the index into the function
table (see `compiler/funcs.c`), and `argc` is the argument count
already on the stack. The VM saves {pc, frame pointer} on the call
stack, sets the new frame pointer to `sp - argc`, and jumps to
`funcs_get_start(fn_idx)`. The call-stack depth is bounded by
`VM_CALL_FRAMES` in `common/config.h` (currently 4); exceeding it
returns `SE_STACK_OVER`.

`OP_RETURN` (value-returning): pops the return value, releases every
slot from `fp` to `sp-1` (locals + arguments), resets `sp = fp`,
restores the caller's {pc, fp}, and pushes the return value at the new
TOS. Compiler emits this when the function declares `-> SomeType`.

`OP_RETURN_V` (void): same as OP_RETURN but pushes `T_NIL` instead of a
caller-provided value, so a void call in expression position still
leaves something on the stack that `OP_POP` (or the REPL auto-result
binding) can consume.

`OP_CALL_BUILTIN` is for `print`, `readLine`, and similar - they're not
implemented as Swift functions and don't go through the function-call
machinery, just dispatched directly from a builtin table.

> **Availability.** "**Extras**" below means the extras interpreters
> (`SWIFTSAT` / `SWIFTAUX`), **not** the lite REPLs. The
> Family B Compiler also recognises and the Family B Runner executes
> every id $0D–$25 (builtins_xlc.c compiled as normal CODE in the
> Runner - no Saturn/aux bank), plus the Family-B-only ids $26-$31.
> the II+ Runner, $25 text80 drives a Videx build when present and
> otherwise degrades to a push-nil no-op.

Builtin IDs (see `src/vm/opcodes.h`):

| ID    | Name              | Notes                          |
|-------|-------------------|--------------------------------|
| $00   | BUILTIN_PRINT     | print(value, ...); appends `\n` |
| $01   | BUILTIN_PRINT_T   | print(value, ..., terminator: "x"); last arg is the terminator string; no implicit newline |
| $02   | BUILTIN_READLINE  | argc=0; pushes T_STR or T_OPT_NIL |
| $03   | BUILTIN_MIN       | argc=2 Ints; pushes the smaller |
| $04   | BUILTIN_MAX       | argc=2 Ints; pushes the larger |
| $05   | BUILTIN_RANDOM_LT | `random(in: a..<b)`; argc=2 Int bounds, pushes Int. **Family B + host only** |
| $06   | BUILTIN_RANDOM_LE | `random(in: a...b)`; argc=2 Int bounds, pushes Int. **Family B + host only** |
| $07   | BUILTIN_ABS       | `abs(_ x: Int) -> Int`. **Family B + host only** |
| $08   | BUILTIN_SGN       | `sgn(_ x: Int) -> Int` (-1/0/1). **Family B + host only** |
| $09   | (reserved, free)  | htab ships as an XLC builtin at $1B; $09 stays free |
| $0A   | (reserved, free)  | vtab ships as an XLC builtin at $1C; $0A stays free |
| $0B   | (reserved, free)  | peek ships as an XLC builtin at $19; $0B stays free |
| $0C   | (reserved, free)  | poke ships as an XLC builtin at $1A; $0C stays free |
| $0D   | BUILTIN_ASC       | argc=1 String; pushes T_INT first byte 0..255. **Extras** (XLC / aux copy-down, table slot 0) |
| $0E   | BUILTIN_CHR       | argc=1 Int 0..255; pushes a 1-byte heap String. **Extras**; compile error on lite |
| $0F   | XLC_OP_STR_CONCAT | internal: relocated OP_STR_CONCAT body, not a user builtin (XLC table slot 2) |
| $10   | XLC_OP_STR_INTERP | internal: relocated OP_STR_INTERP_I body (XLC table slot 3) |
| $11   | BUILTIN_STR_TO_INT| argc=1 String; pushes T_INT or T_OPT_NIL. **Extras** |
| $12   | BUILTIN_ARR_REMOVE_LAST | argc=1 (receiver array on TOS); pops + returns last element, in place; SE_RUNTIME on empty. **Extras** |
| $13   | BUILTIN_ARR_REMOVE_ALL  | argc=1; releases all elements, count→0 in place, pushes nil. **Extras** |
| $14   | BUILTIN_ARR_CONTAINS    | argc=2 (array + needle); pushes Bool under OP_EQ value equality. **Extras** |
| $15   | XLC_OP_NEW_ARRAY  | internal: relocated OP_NEW_ARRAY body (XLC table slot 8). Dual-copy - lite keep the body inline in vm.c. Operand `n` rides the dispatch-arg slot |
| $16   | XLC_OP_ARR_LEN    | internal: relocated OP_ARR_LEN (`.count`) body (XLC table slot 9). Dual-copy, same as $15 |
| $17   | XLC_OP_CALL_BUILTIN | internal: relocated OP_CALL_BUILTIN core bodies - print/print_t/readLine/min/max (XLC table slot 10). Dual-copy. The actual builtin id rides the `xlc_builtin_id` transport (argc rides xlc_argc); ids ≥ BUILTIN_XLC_FIRST keep their own slots |
| $18   | BUILTIN_HOME      | argc=0; clears the text screen via platform_clear_screen(), pushes nil. **Extras** (table slot 11) |
| $19   | BUILTIN_PEEK      | argc=1 Int addr; pushes T_INT byte 0..255 (cc65 reads main RAM directly; host returns 0). **Extras** (table slot 12) |
| $1A   | BUILTIN_POKE      | argc=2 Ints (addr, value); writes the low byte to main RAM (cc65) / no-op (host), pushes nil. `poke(49200,0)` clicks the speaker. **Extras** (table slot 13) |
| $1B   | BUILTIN_HTAB      | argc=1 Int 1..40; moves the text cursor to that column (cc65 conio gotoxy / host no-op), pushes nil. SE_RUNTIME out of range. **Extras** (table slot 14) |
| $1C   | BUILTIN_VTAB      | argc=1 Int 1..24; moves the text cursor to that row, pushes nil. SE_RUNTIME out of range. **Extras** (table slot 15) |
| $1D   | BUILTIN_GR        | argc=0; enter Applesoft-standard mixed GR (40×40 blocks + 4-line text window), clear, colour=0, cursor into the window, pushes nil. **Extras** (table slot 16) |
| $1E   | BUILTIN_TEXT      | argc=0; back to 40-col text mode + clear, pushes nil. **Extras** (table slot 17; also recognised by SWIFTIIE lite for 80-col reversion) |
| $1F   | BUILTIN_COLOR     | argc=1 Int 0..15; sets the current GR colour, pushes nil. SE_RUNTIME out of range. **Extras** (table slot 18) |
| $20   | BUILTIN_PLOT      | argc=2 Ints (x 0..39; y 0..39 mixed / 0..47 full); writes one colour block to the GR page, pushes nil. SE_RUNTIME out of range (y bound follows the active gr/grFull mode). **Extras** (table slot 19) |
| $21   | BUILTIN_GR_FULL   | argc=0; enter full-screen GR (40×48, no text window - MIXCLR $C052), clear, colour=0, pushes nil. **Extras** (table slot 20) |
| $22   | BUILTIN_HLIN      | argc=3 (x1, x2, y); draws a horizontal block run in the current colour (endpoints either order), pushes nil. SE_RUNTIME out of range. **Extras** (table slot 21) |
| $23   | BUILTIN_VLIN      | argc=3 (y1, y2, x); vertical block run, pushes nil. SE_RUNTIME out of range. **Extras** (table slot 22) |
| $24   | BUILTIN_SCRN      | argc=2 (x, y); pushes the GR colour 0..15 at (x,y) (host 0). SE_RUNTIME out of range. **Extras** (table slot 23) |
| $25   | BUILTIN_TEXT80    | argc=0; switch to 80-column text when the active machine/build supports it (//e firmware path or II+ Videx path), pushes nil. No-op on Family B II+ without Videx. |
| $26   | BUILTIN_READ_FILE | `readFile(_ path: String) -> String?` - argc=1 String; pushes the file's bytes as a heap String (capped at `USERFILE_READ_CAP` = 512 B/call) or `T_OPT_NIL` if it can't be opened. **Family B Runner only** (`WITH_SWB` - the Family A interpreters have no MLI for files). Raw MLI via `src/runtime/file_io.c` |
| $27   | BUILTIN_WRITE_FILE| `writeFile(_ path: String, _ s: String) -> Bool` - argc=2 Strings; creates/truncates `path` (ProDOS TXT), writes `s`, pushes Bool success. **Family B Runner only** (`WITH_SWB`), same dialect-fork caveat as $26 - a program using these compiles on a Family B disk and errors in the REPL |
| $28   | BUILTIN_DELETE_FILE | `deleteFile(_ p)` / `deleteDirectory(_ p)` - argc=1 String → Bool; MLI DESTROY (refuses a non-empty directory). Two compiler names, one id. **Family B (`WITH_SWB`)** |
| $29   | BUILTIN_RENAME_FILE | `renameFile(_ old, _ new)` - argc=2 Strings → Bool; MLI RENAME (same volume). **Family B** |
| $2A   | BUILTIN_FILE_EXISTS | `fileExists(_ p)` - argc=1 String → Bool; MLI GET_FILE_INFO. **Family B** |
| $2B   | BUILTIN_APPEND_FILE | `appendFile(_ p, _ s)` - argc=2 Strings → Bool; OPEN (create if absent) + SET_MARK to EOF + WRITE. **Family B** |
| $2C   | BUILTIN_CREATE_DIR  | `createDirectory(_ p)` - argc=1 String → Bool; MLI CREATE storage-type $0D. **Family B** |
| $2D   | BUILTIN_LIST_DIR    | `listDirectory(_ p)` - argc=1 String → `[String]`; reads the ProDOS directory blocks, pushes a heap array of entry names (empty on open failure - the 1-byte ctype can't carry `[String]?`). **Family B** |
| $2E   | BUILTIN_WAIT        | `wait(_ ms: Int)` - argc=1 Int; busy-wait roughly that many milliseconds, pushes nil. **Family B + host only** |
| $2F   | BUILTIN_TONE        | `tone(_ halfPeriod: Int, _ cycles: Int)` - argc=2 Ints; blocking square-wave speaker tone, pushes nil. **Family B + host only** |
| $30   | BUILTIN_HAS_PREFIX  | `s.hasPrefix(_ t: String) -> Bool`; argc=2 (receiver + prefix). **Family B + host only** |
| $31   | BUILTIN_HAS_SUFFIX  | `s.hasSuffix(_ t: String) -> Bool`; argc=2 (receiver + suffix). **Family B + host only** |

Ids $26-$31 are Family-B-only program builtins. The file/directory block
($26-$2D) is intercepted at the top of `OP_CALL_BUILTIN` (`vm_file_builtin`,
gated `WITH_SWB`) so the Runner and the host share one execution path. The
system/string-convenience block ($2E-$31) rides the core-builtin dispatch path.
Family B never uses per-id XLC table slots for these ids. Separately, the
SWIFTSAT REPL reserves the same numeric value `$26` as the internal
`XLC_OP_REPL_READLINE` dispatch slot; it is not a Swift builtin and cannot
coexist with the Family-B file builtin block in one build.

`String(_ n: Int) -> String` does not consume a builtin slot: the
compiler emits `OP_STR_INTERP_I` after parsing the Int argument, which
already handles the Int → heap-string conversion polymorphically.

### Optional handling ($70-$7F)

| Hex | Name           | Operand | Stack             | Notes                |
|-----|----------------|---------|-------------------|----------------------|
| $70 | OP_UNWRAP      | -       | ( opt -- v )      | runtime err (SE_RUNTIME) on nil, otherwise no-op |
| $71 | OP_IS_NIL      | -       | ( opt -- bool )   | reserved (not emitted; a future type-checker pass will route here) |
| $72 | OP_NIL_COALESCE| i16     | ( opt -- v )      | non-nil → keep & jump past default; nil → pop & fall through |
| $73 | OP_IF_LET      | i16     | ( opt -- v )      | non-nil → keep & fall through (bind); nil → pop & jump past body |

`OP_IS_NIL` is reserved at $71 but not emitted - `x == nil`
desugars to OP_EQ against OP_NIL, which costs one extra opcode but
avoids carrying a second nil-test code path through the VM until the
type checker can route it intelligently.

`OP_NIL_COALESCE` peeks at TOS. If not nil, it jumps forward by the i16
offset (skipping the default expression). If nil, it pops and falls
through to evaluate the default. The offset is relative to the byte
immediately after the two operand bytes.

`OP_IF_LET` is the workhorse for `if let` - it inspects the top
optional. If not nil, it leaves the value on the stack (for the binding)
and falls through to the body. If nil, it drops the optional and jumps
forward by the i16 offset.

The opcode is unchanged across the `if let` extensions (else arm
+ function-body bindings): only the compiler's codegen
differs. With an `else`, OP_IF_LET's nil-jump targets the else arm and
the some arm ends with an OP_JUMP over it. Inside a function body the
binding is a **local** - OP_IF_LET already leaves the unwrapped value
on the stack at exactly the next slot, so no store opcode is emitted;
the some arm pops the binding (and any block locals) with OP_POP at the
closing `}`, converging with the nil arm at the same stack depth. At top
level the binding is still a global via OP_DEFINE_GLOBAL.

**Implementation note**: `OP_NIL_COALESCE` and `OP_IF_LET`
share a single dispatch case in `src/vm/vm.c`. The two opcodes are
exact mirrors of each other (non-nil keeps the value, nil pops; only
the jump direction flips), so the VM merges them with a one-byte flag
derived from the opcode, saving CODE.

`OP_OPT_SOME` and `OP_OPT_NONE` are reserved for future typed-optional
emission. Today the compiler emits normal values / `OP_NIL`; executing
either reserved opcode is a bad-opcode error.

### Heap object operations ($80-$8F)

| Hex | Name             | Operand | Stack                | Notes               |
|-----|------------------|---------|----------------------|---------------------|
| $80 | OP_STR_CONCAT    | -       | ( s1 s2 -- s )       | new string; today the live concat path is reached via polymorphic `OP_ADD` (see below) |
| $81 | OP_STR_LEN       | -       | ( s -- int )         | reserved; string `.count` uses OP_ARR_LEN's shared array/string body today |
| $82 | OP_STR_INTERP_I  | -       | ( v -- str )         | polymorphic: convert any TOS to heap string (see below) |
| $83 | OP_STR_INTERP_B  | -       | ( v -- str )         | reserved; not emitted / no VM case today |
| $84 | OP_STR_INTERP_O  | -       | ( v -- str )         | reserved; not emitted / no VM case today |
| $85 | OP_NEW_ARRAY     | u8      | ( v1...vN -- arr )   | u8 element count; n=0 allocates an empty array with default capacity |
| $86 | OP_ARR_GET       | -       | ( arr i -- v )       | bounds-checked; out-of-range returns SE_RUNTIME |
| $87 | OP_ARR_SET       | -       | ( arr i v -- nil )   | subscript assignment; emitted for `xs[i] = v` |
| $88 | OP_ARR_LEN       | -       | ( arr -- int )       |                      |
| $89 | OP_ARR_APPEND    | -       | ( arr v -- arr' )    | grows array; pushes the (possibly relocated) array reference so the compiler can write it back to the source variable |

**OP_STR_INTERP_*** is polymorphic today. The single live
implementation dispatches on the TOS tag: Int → digits, Bool →
`true`/`false`, Nil/Optional-Nil → `nil`, String → pass-through. The
single-pass compiler emits `OP_STR_INTERP_I` for any `\(expr)`. Future
typed bytecode may split the three interpolation opcodes per type to
skip the dispatch - or may keep the current single polymorphic path if
the dispatch cost is negligible against the bytecode-size saving. See
`docs/contributing/design/002-heap-and-strings.md`.

**OP_ADD is polymorphic on strings.** When both operands are `T_STR`,
the VM dispatches to the heap concat path (the same one
`OP_STR_CONCAT` would). The single-pass compiler emits `OP_ADD` for
`+` without tracking operand types. `OP_STR_CONCAT` stays defined for
future typed bytecode.

**OP_RETAIN / OP_RELEASE are emitted by nothing and have no VM dispatch
case today.** Refcount management is implicit in the VM stack ops
(`OP_POP`, `OP_DUP`, `OP_OVER`, and the global/local ops inspect the tag
and call into `value_retain` / `value_release`). The explicit opcode
slots are reserved for a future typed ownership pass. See
`docs/contributing/design/002-heap-and-strings.md`.

### Refcount management ($90-$9F)

Reserved for a future explicit ownership pass. They are not emitted and
will halt as bad opcodes if present in bytecode today.

| Hex | Name         | Operand | Stack    | Notes                       |
|-----|--------------|---------|----------|-----------------------------|
| $90 | OP_RETAIN    | -       | ( v -- v)| inc refcount of heap obj    |
| $91 | OP_RELEASE   | -       | ( v -- ) | dec refcount; free if zero  |

### REPL instrumentation ($E0-$EF)

No opcodes today. `$E0` is free; it once held `OP_REPL_ECHO` (a
`name: Type = value` echo after every binding), since removed - see
design doc 005 and ROADMAP "Maybe / probably never" item 26.

### Reserved ranges

- **$A0-$BF** - reserved for Tier 2 (closures, structs, dictionaries,
  switch).
- **$C0-$DF** - reserved for Tier 3 (classes, protocols, error
  handling).
- **$E1-$EF** - reserved for future debug/profiling instrumentation
  (REPL/dev-only opcodes that file-mode programs never see).
- **$F0-$FE** - reserved for future use.
- **$FF**     - reserved as a "trap" / illegal opcode marker. The VM
  halts with an internal-error message if it ever dispatches $FF.

---

## Encoding examples

A simple expression `1 + 2`:

```
03 01       OP_INT_U8 1
03 02       OP_INT_U8 2
30          OP_ADD
```

A `let x = 1 + 2` followed by `print(x)` (assuming `x` is global #0
and `print` is builtin #0):

```
03 01       OP_INT_U8 1
03 02       OP_INT_U8 2
30          OP_ADD
22 00       OP_DEFINE_GLOBAL #0
20 00       OP_GET_GLOBAL #0
63 00 01    OP_CALL_BUILTIN print, argc=1
```

A `while x < 10 { x = x + 1 }` (x is global #0):

```
LOOP:
20 00       OP_GET_GLOBAL #0
03 0A       OP_INT_U8 10
42          OP_LT
51 .. ..    OP_JUMP_IF_FALSE END   ; skip body if x>=10
20 00       OP_GET_GLOBAL #0
36          OP_INC
21 00       OP_SET_GLOBAL #0
53 .. ..    OP_LOOP LOOP            ; back to top
END:
```

---

## Adding a new opcode

1. Pick a free hex value in the right group.
2. Add a row to the table above with operand encoding, stack effect,
   and a one-line note.
3. Add the symbol to `src/vm/opcodes.h`.
4. Implement the case in `src/vm/vm.c` (and the assembly dispatch
   table in `src/vm/dispatch.s` if you're modifying hot paths).
5. Add a disassembler case in `tools/host/disasm/disasm.c`.
6. Add a unit test in `tests/sim/` that runs a tiny program using the
   opcode and checks the result.
7. If a Swift-language feature now compiles to it, update
   `LANGUAGE.md` if the feature wasn't already listed.

If your opcode would need more than 2 operand bytes, stop and
redesign - that's a sign the operation should be a builtin call
instead.
