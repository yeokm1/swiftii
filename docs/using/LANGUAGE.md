# LANGUAGE.md - SwiftII Language Specification

SwiftII implements a deliberately small subset of Swift. This document is
the source of truth for what the language *is*. If a Swift feature isn't
listed here, it's not supported. If it's listed under a later tier, it's
planned but not built.

Where SwiftII deviates from real Swift, those deviations are listed in
[Differences from Swift](#differences-from-swift) at the bottom. The goal
isn't conformance; the goal is that a Swift programmer reading SwiftII
code recognizes it instantly.

---

## Execution modes

SwiftII has two front doors. Both run the same compiler and VM.

### REPL mode

Invoked with no arguments:

```
] -SWIFTIIP
SwiftII ][+ 1.0.2
Type :help :list :quit
> let name = "Woz"
> print("Hello, \(name)!")
Hello, Woz!
> 1 + 2
3
> 
```

REPL semantics:

- Each input is parsed and executed immediately. The prompt is a
  plain `> `; no submission numbering.
- Top-level statements share a single global scope across the session.
- Variable bindings made with `let` and `var` persist; `let` / `var` /
  assignment statements are **silent** (no echo).
- Top-level bare expressions in REPL mode are implicitly printed -
  `1 + 2` yields `3`, matching the BASIC / Python convention. File
  mode discards bare-expression values.
- Function definitions persist and are callable from later input.
  Redefining a name (`func foo()` when `foo` already exists) **rebinds it
  to the new body** and prints a `redef foo` notice - **on the //e binaries
  only** (SWIFTIIE / SWIFTAUX). The new definition may change the parameter
  list / return type, and later calls resolve to it. If the new body fails
  to compile, the previous definition is kept unchanged (the redefinition
  is atomic). On the **II+ binaries** (SWIFTIIP / SWIFTSAT) redefining a
  name still **errors** - they sit at the ProDOS file ceiling and can't
  absorb the ~340 B of compiler-side code - so use `:reset` to clear the
  session and redefine there (see ROADMAP). Each //e redefinition
  leaves the previous body as dead space in the function arena; `:reset`
  reclaims it.
- Multi-line input is **not** supported in the REPL for v1: each
  input line must be a complete statement (or block-less single
  expression). Block-defining constructs (`if`/`while`/`for-in`/
  `func`) must fit on one line, optionally separated by `;`. The
  launcher editor (not a standalone binary) is the home for multi-line
  authoring; file mode runs whole `.SWIFT` files.
- A runtime error prints a message and returns to the prompt without
  losing previously defined globals or functions.
- A compile error in one statement does not affect the session state.
- **Line editing.** The left-arrow / **Backspace** key (`$08`) deletes the
  character to its left on every binary. On the **//e binaries only**
  (SWIFTIIE / SWIFTAUX) the prompt also recalls earlier input lines:
  **up-arrow** (or **Ctrl-P**) walks back through history and **down-arrow**
  (or **Ctrl-N**) walks forward, with down past the newest restoring the
  line you were typing. A recalled line can be edited and re-run like any
  other.

  **Capacity:** the **8 most recent input lines** are remembered, each up
  to **127 characters** long (a longer line still runs, it just isn't added
  to history); the 9th line pushes the oldest out, and an exact repeat of
  the previous line isn't stored twice. History is a fixed ~1 KB ring in
  RAM, cleared on every cold boot / relaunch - it does not persist to disk
  and `:reset` does not affect it.

  The II+ binaries (SWIFTIIP / SWIFTSAT) keep just the backspace edit -
  they have no up/down keys and sit at the ProDOS file ceiling, so history
  is //e-only (the ring buffer is ~1 KB of BSS the II+ REPLs can't spare;
  see ROADMAP).

REPL meta-commands (start with `:`, not part of the language):

- `:help` - list commands
- `:quit` - exit (or press **Ctrl-D** on an empty line - EOF, same exit)
- `:reset` - clear all bindings (user globals + function table) and
  reset the heap
- `:mem` - print used / free heap bytes
- `:list` - list current bindings in definition (chronological) order
  as `let name = value` / `var name = value`

`:list` prints the name and current value, not the declared type;
rendering the type would pull in two ctype formatters (~550 B) the
budget-tight II+ binaries can't keep. The REPL also has no numbered
prompts, binding-echo, or `$R<n>` auto-results (à la Swift LLDB) - see
ROADMAP "Maybe / probably never" item 26 and design doc 005.

### File mode

On Apple II disks, file mode is launched from the boot launcher's file
selector, not from a ProDOS command line. Highlight a `.SWIFT` file and
press **X**:

- **Family A REPL disks** stage up to 2 KB of source in low RAM, chain the
  resident interpreter, and run that staged source once before returning to
  the prompt.
- **Family B compiler disks** stream source from disk through the standalone
  Compiler, write a `.SWB` bytecode file next to the source, then chain the
  Runner.

On the host test binary only, `argv[1]` selects direct file mode.

File mode semantics:

- The selected source is lexed, parsed, and compiled before execution begins
  (in one staged buffer on Family A; streamed to `.SWB` on Family B).
- Top-level statements execute in source order.
- The program ends when the last top-level statement returns or when an
  uncaught runtime error occurs.
- On Family A staged runs, the same REPL compile path is reused, so a final
  bare expression still prints. Family B and host file runs discard
  bare-expression values.
- Host exit code is 0 on clean completion, nonzero on error. On Apple II the
  launcher/Compiler/Runner returns to the launcher UI instead of exposing a
  shell-style exit code.

A SwiftII source file conventionally has the extension `.SWIFT` on
ProDOS (or `.swift` on the host). A compiled bytecode file has the
extension `.SWB` (Swift Bytecode), produced by the on-disk Family B
Compiler or by the host `swbc` tool.

---

## Lexical structure

### Source encoding

ASCII only. Apple II keyboards predate Unicode by decades. A non-ASCII
byte in source code is a lex error.

`.SWIFT` source files on disk are **canonical lowercase ASCII**
regardless of which machine wrote them. A file written on an Apple
II Plus is byte-identical to a file written on a //e or a Mac with
the same content. This invariant is enforced by the input layer
(below) on the //+ side; host and //e keyboards already produce
canonical bytes.

### Typing on Apple II Plus

The stock //+ keyboard produces only uppercase letters and a
restricted punctuation set; it can't type lowercase, can't type
`{ } [ ] \ _`, and can't type `| ` `` ` `` `~`. SwiftII solves both
problems with a **machine-dependent input layer** that runs above
the keyboard and below storage. Source files always end up in
canonical lowercase ASCII; users on different machines just have
different paths to producing those bytes.

**On //e and later**, the keyboard already produces canonical bytes.
Type Swift code normally.

**On //+ and the original Apple II**:

- Letter keys auto-lowercase by default. Typing `LET X = 5` produces
  canonical `let x = 5`.
- An apostrophe `'` before a letter marks it as uppercase. Typing
  `'INT` produces canonical `Int`. (One uppercase, then two
  auto-lowercased.)
- A double apostrophe `''` marks a *run* of uppercase letters
  through the next non-letter. Typing `''MAX +` produces `MAX +`.
- `Ctrl-W` produces `_`.
- C-standard digraphs supply the bracket and slash characters that
  the //+ keyboard can't type:

| Sequence | Result |
|----------|--------|
| `<%`     | `{`    |
| `%>`     | `}`    |
| `<:`     | `[`    |
| `:>`     | `]`    |
| `??/`    | `\`    |
| `??!`    | `\|` (reserved; not used in SwiftII Tier 1) |

**Apostrophe inside string literals**: a contextual rule covers the
two real-world cases:

- `'` typed between two letters is a literal apostrophe (so
  `"DON'T WORRY"` stores as `"don't worry"`).
- `'` typed after a non-letter (including the opening `"`) is a case
  marker for the next letter (so `"'HELLO"` stores as `"Hello"`).

**Apostrophe and digraphs inside comments**: auto-lowercase still
applies inside `//` to end-of-line and `/* */` comments, but
apostrophes are always literal and digraph sequences pass through
unchanged. This lets a comment document the typing model without
being mangled by it.

### Display on Apple II Plus

The pre-IIe character ROM has glyphs only for ASCII `$20`–`$5F`, so
lowercase letters and `{ } |` have no native rendering. The display
layer substitutes:

- Lowercase letters → normal-video uppercase; real uppercase letters
  → inverse-video uppercase. Lowercase dominates Swift source, so the
  common case stays unhighlighted and only the rarer uppercase is
  flagged inverse - e.g. canonical `readLine` shows with just its `L`
  in inverse video, making case visually distinct at a glance.
- `{` → `<%`, `}` → `%>`, `|` → `??!` (rendered as their digraph
  forms - two or three columns wide).

The stored bytes are never altered; only the on-screen rendering on
pre-IIe machines differs. `s.count` on `"hello{}"` returns 7 on
every machine, even though the display width on //+ is two columns
longer per `{` or `}`.

**Plain text files are exempt.** Everything above - the typing
transforms *and* this pre-IIe display layer - applies only to `.swift`
source. When the editor or the file-browser preview opens a file with
any other name (for example the on-disk `README.TXT` help), it is
shown and saved as **plain native text**: no auto-lowercase or case
markers, no inverse-video swap, no digraph expansion. That is why the
II+ program disks ship an all-caps `README.TXT` - on a machine with no
lowercase glyphs it reads as ordinary normal-video text.

On //e, //c, //c+, and //gs (machines with the enhanced character
ROM), every byte renders natively in its real glyph and case.

See [`docs/contributing/design/003-apple2-input-method.md`](../contributing/design/003-apple2-input-method.md)
revision 3 for the full rationale, the input-layer state machine,
and the implementation notes.

### Whitespace and line endings

Spaces, tabs, and newlines are whitespace. Either `\n` or `\r` ends a
line (ProDOS uses `\r`, the host typically uses `\n`; the lexer accepts
both). Whitespace separates tokens but is otherwise insignificant -
SwiftII does not have Python-style indentation.

### Comments

```swift
// line comment, runs to end of line
/* block comment, may span lines */
```

Block comments do not nest. (Real Swift's block comments nest; ours
don't, to save a lexer state byte.)

### Identifiers

```
ident ::= [A-Za-z_][A-Za-z0-9_]*
```

ASCII letters, digits, and underscore. Case-sensitive. The symbol
tables store **11 significant characters** per identifier (`IDENT_MAX` is 12,
with one byte reserved for padding / termination). Declaring a longer name is
a **compile-time error** (`name >11 chars`) - it is *not* silently
truncated, so distinct names can never collide on a shared prefix.

This 11-character cap is a deliberate Apple II memory trade-off from
`docs/contributing/design/004-demo-oriented-scope.md`, not a Swift compatibility promise.

### Reserved words

```
let var func return if else while for in break
true false nil
struct                    (Tier 2)
guard                     (Tier 2, may be cut - see ROADMAP.md)
continue                  (reserved for future use; not in v1)
```

Reserved words may not be used as identifiers. The Tier-3 keywords
(`class`, `protocol`, `enum`, `throws`, `try`, `catch`) are *not*
reserved in v1. If a future tier brings them back, source that names
variables `class` etc. will need to be updated.

### Literals

```swift
42          // decimal integer
-7          // negative integer (unary minus + literal)
true false  // booleans
nil         // the absent value
"hello"     // string
"x = \(x)"  // string with interpolation
```

Integer literals accept the **unsigned 16-bit range 0..65535** at the
lexer; the Int type itself is signed i16 (-32768..32767). Values in
32768..65535 are stored as their two's-complement i16 bit pattern -
`49200` becomes the Int with `lo=$30 hi=$C0`, which displays as
`-16336` if `print`ed but reads back as `49200` (u16) under
two's-complement bit-pattern interpretation. This lets the `peek` /
`poke` builtins take Apple II addresses typed
naturally as decimal: `49200` for the speaker click at $C030,
`49250` for paddle 0 at $C062. Negative literals like `-32768` are
also accepted via unary minus on a positive value (parsed by the
Pratt expression layer, not the lexer). A literal outside 0..65535
is a compile error.

**Decimal only** in v1 - there are no hex (`0xFF`), binary (`0b1010`),
or octal (`0o17`) literal forms. The u16 decimal range covers the
`peek`/`poke` address-typing need at zero binary cost vs the ~150 B a
hex prefix would add. Underscore digit separators (`1_000_000`) are
also unsupported.

SwiftII numbers are integers only; there is no fractional or
decimal-point literal form.

String literals support these escapes: `\\`, `\"`, `\n`, `\t`, `\r`,
`\0`, `\(expr)`. Other backslash sequences are an error.

### Operators

In precedence order, lowest to highest:

```
=                                           assignment
||                                          logical or
&&                                          logical and
== !=                                       equality
??                                          nil-coalescing
< <= > >=                                   comparison
+ -                                         additive (binary)
* / %                                       multiplicative
- !                                         unary prefix
.                                           member access
( ) [ ]                                     call, subscript
```

`=` is a statement, not an expression - `let x = (y = 1)` is a syntax
error, by design.

`&&`, `||`, and `??` short-circuit: the right-hand side is evaluated
only when the left-hand side doesn't already decide the result. So `b`
runs only if `a` is true in `a && b`, only if `a` is false in `a || b`,
and only if `a` is nil in `a ?? b`. `&&` and `||` take `Bool` operands
and produce a `Bool`; use prefix `!` to negate. (Like the rest of
SwiftII's lightweight typing this is best-effort: the left operand is
checked at run time, but a non-`Bool` right operand on the evaluated
path passes through unchecked — keep both sides `Bool`.)

```swift
if a > 0 && b > 0 { foo() }
if a == 1 || b == 1 { foo() }
let name = maybeName ?? "anon"
```

### Ranges

```swift
0..<10      // half-open: 0,1,...,9
0...10      // closed:    0,1,...,10
```

Ranges exist only as iterable values for `for-in`. They are not
first-class in Tier 1 (cannot be stored in variables).

---

## Types

### Built-in types

| Name     | Description                              | Size on heap |
|----------|------------------------------------------|--------------|
| `Int`    | 16-bit signed integer                    | inline tag   |
| `Bool`   | `true` or `false`                        | inline tag   |
| `String` | immutable UTF-8 (well, ASCII) byte seq.  | 4+N bytes    |
| `Array`  | homogeneous, growable                    | 6+N bytes    |
| `T?`     | optional of T (`Int?`, `String?`, etc.)  | inline tag   |
| `Void`   | no-return function result                | inline tag   |

`Int` and `Bool` and `nil` are *immediates* - stored directly in the
3-byte tagged value, never on the heap.

`String` and `Array` are *heap objects* - the value contains
a pointer to a heap allocation, with reference counting.

### Type inference

```swift
let x = 5          // x: Int
let s = "hi"       // s: String
let xs = [1, 2, 3] // xs: [Int]
let y: Int? = nil  // explicit annotation needed; nil alone is ambiguous
```

A binding without an explicit type takes the type of its initializer. A
binding without an initializer requires an explicit type:

```swift
var x: Int           // OK, must be assigned before use
var y                // error: type required
```

The compiler tracks types statically for local variables and function
signatures. Globals follow the same rule.

### Optionals

```swift
let maybe: Int? = 5
let nothing: Int? = nil

if let x = maybe {
    print(x)         // 5, unwrapped
}

let n = maybe ?? 0   // nil-coalescing default
let f = maybe!       // force-unwrap; runtime error if nil
```

Optionals are SwiftII's most distinctive feature relative to other
small languages. They're worth getting right.

---

## Statements

### Variable declarations

```swift
let name = "Steve"      // immutable
var count = 0           // mutable
var ready: Bool = false // explicit type
```

`let` bindings cannot be reassigned. The compiler enforces this; an
attempt is a compile error.

### Assignment

```swift
count = count + 1       // OK if count is var
name = "Woz"            // error: name is let
```

Compound assignments desugar to `x = x <op> y`:

```swift
count += 1              // OK if count is var; equivalent to count = count + 1
total -= price          //    "
score *= 2              //    "
half /= 2               //    "
```

Only `+=`, `-=`, `*=`, `/=` are recognised. `%=` and bitwise compound
forms are not in Tier 1. `+=` on a `String` concatenates (`s += "x"`).
Compound assignment compiles to `x = x <op> y` on **all** binaries.
See `src/compiler/statements.c` (`SWIFTII_EXT_COMPILER`).

### `if` / `else`

```swift
if x > 0 {
    print("positive")
} else if x < 0 {
    print("negative")
} else {
    print("zero")
}
```

The condition must be a `Bool`. Non-Bool conditions (e.g., truthy ints)
are a compile error.

### `if let`

```swift
if let value = maybeInt {
    print(value)
}
```

The Swift 5.7+ shorthand is also accepted when the bound name matches
an in-scope optional:

```swift
let maybeInt: Int? = 5
if let maybeInt {           // shorthand for `if let maybeInt = maybeInt`
    print(maybeInt)         // bound name shadows the outer optional, unwrapped
}
```

An `else` arm runs when the optional is nil, and `if let` works inside
function bodies:

```swift
func parse(_ s: String) -> Int {
    if let n = Int(s) {
        return n
    } else {
        return -1          // taken when Int(s) is nil
    }
}
```

Inside a function the bound name is a local scoped to the `if` block -
it is out of scope in the `else` arm and after the block. At top level
(REPL / file scope) the binding is a global and, as a known
carry-over, stays visible after the block (reading as the unwrapped
value on the some path; untouched on the nil path).

Tier 1 supports the simple form: a single binding from a single
optional, no chained bindings, no `where` clause. Multi-bind comes in
Tier 2.

### `while`

```swift
while count < 10 {
    count = count + 1
}
```

### `for-in`

```swift
for i in 0..<10 {
    print(i)
}

// Walk an array by index (see note below):
for i in 0..<items.count {
    print(items[i])
}
```

Iterates over a **range** (`a..<b` or `a...b`).

```swift
// Direct array iteration - Family B Compiler/Runner only:
for item in items {
    print(item)
}
```

Iterating an `Array` directly (`for item in items`) is a **Family B
feature**: it binds each element in order. It
compiles on a Family B disk (the standalone Compiler) but is **rejected by
the Family A REPL interpreters** with `want '..<'` - they sit at the
64 K ceiling and the desugar's ~444 B of compiler code doesn't fit. On the
REPL, walk the array by its index range (`for i in 0..<items.count`).
Other iterables come in Tier 2.

### `break`

```swift
var i = 0
while true {
    i = i + 1
    if i > 10 { break }
    print(i)
}
```

`break` exits the innermost enclosing `while` or `for-in` loop and
jumps to the first statement after the loop. `break` outside a loop
is a compile error.

`continue` is reserved but not implemented in v1 - use an `if`
that guards the rest of the loop body if you need similar behaviour.

### `switch` (Family B Compiler/Runner only)

```swift
switch n {
case 0:
    print("zero")
case 1, 2, 3:          // comma-grouped values
    print("small")
default:
    print("other")
}
```

A **Family B feature**: it compiles on a
Family B disk but is **rejected by the Family A REPL interpreters** (same
64 K ceiling as direct array `for-in`). Matches an `Int` or `Bool` value
against literal `case` patterns; a `case` may list several comma-separated
values. Each case body runs to the next `case`/`default`/`}` - there is
**no fall-through** (and so no need for `break`; an explicit `break` inside
a case refers to an enclosing loop, not the switch). `default:`, if
present, must be the last clause; with no `default` and no match, the
switch does nothing. Matching uses the same comparison as `==`, so
**`switch` on `String` is not supported** (string `==` compares identity,
not content - the compiler rejects a `String` switch with `switch
Int/Bool`). Nested `switch` is not supported.

### `return`

```swift
return            // returns from a void function
return 42         // returns a value
```

`return` outside a function is a compile error. A function whose return
type is non-void must end with a `return` on every path; the compiler
checks this.

#### Implicit return - not in v1

Swift 5.1+ lets a single-expression function body omit `return`
(`func square(_ x: Int) -> Int { x * x }`). SwiftII v1 does **not**:
every value-returning function must use an explicit `return`, and a body
that falls off the end is a `missing return` compile error.

```swift
func square(_ x: Int) -> Int { return x * x }     // required form
func square(_ x: Int) -> Int { x * x }            // ERROR: missing return
```

Implicit return is deferred ("Maybe / probably never" item 12): it costs
~274 B of compiler code that doesn't fit the II+ binaries at the 64 K
ceiling, and it's pure sugar over the explicit form.

---

## Functions

```swift
func greet(person: String) -> String {
    return "Hello, \(person)!"
}

func square(_ x: Int) -> Int { return x * x }
let n = square(7)
```

**Calls on the Apple II are positional.** Declarations may still name
parameters (and even give external labels, `func power(of base: Int,
to exp: Int)`), but a call passes arguments by position only -
`power(2, 10)`, not `power(of: 2, to: 10)`. Argument labels at the call
site are accepted **only on the development host build** (`swiftii_host`),
where they are ignored; the four Apple II binaries reject them. The
label-acceptance lookahead (plus the per-argument type check it fed) is not
on the target (the II+ lite binary has no banking escape valve). Argument
labels are a `Maybe / probably never` item - re-addable on the extras
binaries via banking if a demo wants them. Because samples ship on disk and
run on hardware, every bundled sample uses positional calls. On a labeled
call the //e Compiler reports `use positional args, not labels`, and all
compile errors print the source line (`compile error: line N: …`). (The II+
baseline shows the line number but keeps the terse message - no RODATA room
for the hint at the 64K ceiling.)

Functions are not first-class values: they cannot be stored in
variables, passed as arguments, or returned. Closures are not supported
in SwiftII v1 - the upvalue-capture machinery costs 3–4 KB of cc65
code that the memory budget cannot spare. See
`ROADMAP.md section Maybe / probably never`.

Function definitions are top-level only. Nested functions are not
supported. (This is a memory-management simplification, not a language
preference; nested functions create scoping concerns we'd rather defer.)

Recursion is supported but bounded by `VM_CALL_FRAMES` (currently 4;
see "Implementation limits"). `fact(4)` works; `fact(5)` returns a
runtime error. The compiler does not currently do tail-call
optimization.

**Limitations:**
- Argument labels: **positional-only on the Apple II target** (see
  above). The host build still accepts `label: expr` at call sites but
  does **not** validate that the label matches the declared parameter
  (`add(a: 1, b: 2)`, `add(b: 2, a: 1)`, and `add(1, 2)` are the same
  call there); the per-argument type check is likewise host-only.
- A `-> SomeType` declared function must return a value on every
  exit path; falling off the end is a compile error. (There is
  no flow analysis, so we conservatively reject any function body
  whose last emitted opcode is not `OP_RETURN`/`OP_RETURN_V`.)
- A void function whose body ends without an explicit `return` is
  auto-terminated with `OP_RETURN_V` and returns `nil` to the caller.

**REPL function persistence**: a function defined on one REPL input
line stays callable on subsequent lines. The compiled function body
lives in a persistent arena at the front of the shared bytecode
buffer; the per-line top-level scratch sits after it. See
`MEMORY_MAP.md section Bytecode buffer layout`. `:reset` clears the arena
along with globals and the heap.

---

## Strings

Strings are immutable. The `+` operator concatenates:

```swift
let a = "foo"
let b = "bar"
let c = a + b           // "foobar"
```

String interpolation uses `\(expr)`:

```swift
let n = 42
let s = "answer: \(n)"  // "answer: 42"
```

Interpolated expressions of type `Int`, `Bool`, `String`, and `T?` are
converted to their natural string form. Other types are a compile
error.

Core members:

- `s.count` - number of bytes (Int)
- `s.isEmpty` - Bool

More string methods come in Tier 2.

---

## Arrays

```swift
var xs = [1, 2, 3]
let first = xs[0]      // 1
let n = xs.count       // 3
xs.append(4)           // xs is now [1, 2, 3, 4]
print(xs)              // [1, 2, 3, 4]
```

Arrays are homogeneous: `[1, "two"]` is a compile error. The element
type is inferred from the literal (or annotated explicitly).

Methods supported in Tier 1:

- `xs.count` - element count (Int)
- `xs.isEmpty` - Bool, equivalent to `xs.count == 0`
- `xs.append(x)` - push. Statement-level `xs.append(x)` updates `xs`
  in place (the compiler emits a write-back after the append so the
  variable tracks any heap relocation). In expression position the
  call returns the (possibly relocated) array reference.
- `xs[i]` - subscript read (runtime error if out of bounds)
- `xs[i] = v` - subscript write; runtime error if out
  of bounds. `let`-bound arrays reject subscript-set as
  `compile error: let is const`, matching scalar `let` behaviour.
- `xs.removeLast() -> Element` - removes and returns the last element
  (runtime error on an empty array). **Extras**: XLC-resident on
  SWIFTSAT, aux copy-down on SWIFTAUX, normal CODE in Family B / host;
  lite reject the call as `unknown member`.
- `xs.removeAll()` - empties the array in place; returns the same `nil`
  placeholder as other void calls. **Extras**.
- `xs.contains(v) -> Bool` - true if some element equals `v` under the
  same value equality as `==` (reference identity for heap strings,
  by value for Int/Bool). **Extras**. All three mutate/read in
  place (same heap offset) so no write-back is needed, unlike
  `.append`.

The REPL does not echo declarations or inferred types; inspect a binding
with `:list`, which prints the current value only. Inferred array element
types are still tracked by the compiler, so `[1, 2, "three"]` is a compile
error and an empty array needs a `[T]` annotation when the use site cannot
infer the element type.

The `[Type]` type annotation (`var xs: [Int] = []`, function param
`[Int]`, return type `[Int]`) parses. `removeLast` / `removeAll` /
`contains` are Extras builtins (see the methods list above);
`map`, `filter`, etc. are not yet supported.

---

## Built-in functions

> **Availability.** "Extras" on the builtins below means the extras
> binaries, **not** the lite REPLs. The
> **Family B Compiler/Runner also recognise and execute the entire
> extras surface** (`asc`/`chr`/`Int(_:)`, the array methods, and all
> platform/GR builtins - builtins_xlc.c compiled as normal CODE in the
> Runner, no Saturn/aux bank needed), which is what lets a graphics
> program compiled on a Family B disk run on a bare 64K machine.
> (`text80()` additionally: SWIFTAUX + SWIFTIIE + Family B; the II+
> Saturn/Runner path drives Videx builds when present and otherwise
> pushes nil as a no-op.)

Tier 1:

- `print(_ value)` - print a value followed by a newline. Accepts any
  type the string interpolation system handles.
- `print(_ value, terminator: String)` - print a value followed by the
  given terminator string instead of `\n`. The terminator is an expression
  that must evaluate to `String`. Common idioms:
  ```swift
  print("Enter name: ", terminator: "")   // inline prompt
  print(score, terminator: " ")            // build a row of values
  ```
- `readLine() -> String?` - read a line from the keyboard (REPL or
  user input). Returns `nil` at EOF.

`min` and `max` are Core builtins. Both take two Ints and return the
smaller / larger:

```swift
print(min(3, 7))   // 3
print(max(-2, 5))  // 5
```

Wrong-typed args (e.g. `min(1, "x")`) fail at compile time as
`type mismatch`. `abs` and `sgn` are Family B program builtins.

**`random(in:)` (Family B Compiler/Runner only)** - a **Family B
feature**: returns a pseudo-random `Int` in a range.

```swift
let die  = random(in: 1...6)    // closed: 1...6 inclusive
let coin = random(in: 0..<2)    // half-open: 0 or 1
```

Takes an `in:` range argument (half-open `a..<b` or closed `a...b`, Int
bounds); an empty/inverted range is a runtime error. The generator is a 16-bit
xorshift. The Apple II has no clock, so - exactly as Applesoft's `RND` relied
on the monitor's keyboard loop bumping its seed - each draw folds in
**keypress-timing entropy** (the count of how long you took to press the last
key), so real runs differ. Programs that draw *before* any input (a "press a
key to start" is the idiom) seed it that way. On the **host** there is no such
entropy, so the sequence is **deterministic** (which keeps the test suite
reproducible). Like the other Family B features it compiles on a Family B disk
but is **rejected by the Family A REPL interpreters** (the deliberate dialect
fork).

---

## Apple II Platform Built-ins

These functions expose Apple II hardware and ROM capabilities. They are
Apple II-only: on the host they are stubbed (no-ops or simple fallbacks)
so that programs still compile and mostly run for testing.

### Integer math

Equivalent to Applesoft's numeric functions, integer-only.

| SwiftII                           | Applesoft          | Notes                              |
|-----------------------------------|--------------------|------------------------------------|
| `abs(_ x: Int) -> Int`            | `ABS(x)`           | absolute value                     |
| `min(_ a: Int, _ b: Int) -> Int`  | -                  | no Applesoft equivalent            |
| `max(_ a: Int, _ b: Int) -> Int`  | -                  | no Applesoft equivalent            |
| `sgn(_ x: Int) -> Int`            | `SGN(x)`           | -1, 0, or 1                        |

### String conversions

| SwiftII                           | Applesoft          | Notes                              |
|-----------------------------------|--------------------|------------------------------------|
| `Int(_ s: String) -> Int?`        | `VAL(s$)`          | parses a decimal string (optional leading `+`/`-`) to an int16; returns `nil` on empty, non-numeric, or out-of-range input. **Extras** - XLC-resident on SWIFTSAT, aux copy-down on SWIFTAUX, normal CODE in Family B / host; lite skip the parser branch. Uppercase Swift convention; the //+ input layer requires `'Int(...)`. |
| `String(_ n: Int) -> String`      | `STR$(n)`          | decimal string representation; compiles to `OP_STR_INTERP_I` so it reuses the same path as `\(n)` interpolation |
| `asc(_ s: String) -> Int`         | `ASC(s$)`          | ASCII value of first byte; runtime error (type mismatch) if empty. **Extras**; lite skip the parser branch entirely. |
| `chr(_ n: Int) -> String`         | `CHR$(n)`          | single-character string for byte `n` (0–255); runtime error if `n` is out of range. Inverse of `asc` - `chr(asc(s))` recovers the first character. **Extras**; lite skip the parser branch entirely. Only 0–127 are standard ASCII on screen; 128–255 render inverse/flashing per the pre-IIe character ROM. |

### Screen control - text mode

| SwiftII                           | Applesoft          | Notes                              |
|-----------------------------------|--------------------|------------------------------------|
| `home()`                          | `HOME`             | clear screen, cursor top-left. **Extras**. Routes to `platform_clear_screen()` |
| `text()`                          | `TEXT`             | back to 40-col text + clear. **SWIFTSAT/SWIFTAUX** (XLC). On //e builds also reverts 80→40 (the symmetric partner of `text80()`) |
| `text80()`                        | `PR#3`             | switch to 80-col text on builds with a supported 80-col device. On both //e binaries via firmware 80-col, and on II+ Saturn/Family B via Videx. No-op unless the active build and runtime hardware support 80-col |
| `htab(_ col: Int)`                | `HTAB n`           | move cursor to column 1–40 (1–80 in 80-col mode, //e or II+ Videx). **SWIFTSAT/SWIFTAUX** (XLC). Runtime error if out of range; cc65 `gotoxy` (40-col) or the firmware cursor column OURCH `$057B` (80-col), host no-op |
| `vtab(_ row: Int)`                | `VTAB n`           | move cursor to row 1–24 - works in 80-col too (//e or II+ Videx). **Extras**. Runtime error if out of range; cc65 `gotoxy` (40-col) or the monitor row CV `$25` (80-col; the //e also re-runs `VTAB $FC22` since its firmware caches the line base, the Videx does not) |
`normal()` / `inverse()` / `flash()` are not shipped builtins. They remain
deferred because the pre-IIe case cue already uses the inverse-video byte
range (capitals render inverse), so text attributes need a careful
screen-layer design rather than a cheap monitor flag write.

80-column text is gated on the build flag `WITH_80COL` (default 1) plus a
runtime card-presence probe. On the **//e disk** (`WITH_IIE`) the
`text80()` builtin ships in `SWIFTAUX` and drives the //e built-in 80-col
firmware; it is a no-op unless a card is detected at runtime, so it is
safe to call on any //e binary. **Both //e binaries come up in 80 columns
by default** when a card is present (probed at init) and **both expose the
same `text80()` / `text()` builtins**, so a Swift *program* can switch
width on either - `SWIFTAUX` runs them as copy-down overlay builtins,
`SWIFTIIE` lite runs them inline in its VM path (it has no XLC). The lite
binary only afforded them after gating the dead `input_translate` (~2 KB,
unused on `WITH_IIE`) out of its image. The II+ lite binary (`SWIFTIIP`)
stays 40-column. The II+ Saturn extras binary (`SWIFTSAT`) and Family B
Runner can drive a Videx Videoterm build; without Videx, `text80()`
degrades to a no-op.
Building with `WITH_80COL=0` compiles the //e 80-col path out entirely (the
40-col path is byte-identical to a build without the flag). See
`docs/contributing/design/013-80col-text.md`.

### Low-resolution graphics - GR mode (16 colors)

Two GR modes are available:

- **`gr()`** enters **mixed** GR, matching Applesoft `GR`: a 40-column ×
  40-row grid of color blocks in the top of the screen, plus a 4-line
  text window at the bottom (rows 20–23) for `print()` output. The
  cursor drops into the text window. `plot` y range is **0–39**.
- **`grFull()`** enters **full-screen** GR: the whole screen is a
  40-column × 48-row grid, no text window (`print()` would draw over the
  art). `plot` y range is **0–47**.

Both clear the graphics area to black and reset the current color to 0.
`text()` returns to full 40-column text and clears. `plot`'s y bound
follows whichever mode is active (switching back to `gr()` after
`grFull()` restores the tighter 0–39 bound).

| SwiftII                                   | Applesoft          | Notes                              |
|-------------------------------------------|--------------------|------------------------------------|
| `gr()`                                    | `GR`               | enter mixed GR (40×40 + text window), clear, color 0. **Extras** |
| `grFull()`                                | `GR` + `POKE -16302,0` | enter full-screen GR (40×48, no text window) |
| `text()`                                  | `TEXT`             | back to 40-col text + clear. **Extras**; also available on SWIFTIIE lite for 80-col reversion |
| `color(_ n: Int)`                         | `COLOR= n`         | set GR color 0–15; runtime error if out of range |
| `plot(_ x: Int, _ y: Int)`                | `PLOT x, y`        | plot one block (x 0–39; y 0–39 mixed / 0–47 full); runtime error if out of range |
| `hlin(_ x1: Int, _ x2: Int, _ y: Int)`    | `HLIN x1,x2 AT y`  | horizontal block run at row y (endpoints either order); runtime error if out of range |
| `vlin(_ y1: Int, _ y2: Int, _ x: Int)`    | `VLIN y1,y2 AT x`  | vertical block run at column x |
| `scrn(_ x: Int, _ y: Int) -> Int`         | `SCRN(x,y)`        | read GR color 0–15 at (x, y) (host returns 0) |

`hlin`/`vlin` take the two endpoints first, then the fixed line
coordinate, all positional - e.g. `hlin(0, 39, 10)` draws row 10 across
the full width. (There is no Swift-idiomatic `at:` label: the
label-parsing code doesn't fit the SWIFTSAT MAIN budget, and the
dispatchers live in XLC.)

`grFull` has a capital F; on a pre-IIe (//+) the input method needs the
case marker - type `gr'full` (the `'` uppercases the next letter), like
`'readLine` / `'String`.

On the host, `gr`/`grFull`/`text`/`color`/`plot` are no-ops (no GR
hardware) apart from the color/coordinate range checks, so graphics
programs run in the test suite but the drawing is verified on the
emulator.

**Full-screen GR and the REPL.** `gr()` (mixed) keeps a 4-line text
window and parks the cursor there, so the REPL prompt and echo stay out
of the artwork - it's the REPL-friendly mode. `grFull()` has no text
window, so the prompt/echo (and any scroll) are drawn *over* the
graphics as stray colored blocks. To view a full-screen drawing
interactively, pause before the prompt returns - e.g. end the line with
`let _ = readLine()` so the screen holds until you press Enter. (A
`.swift` program run from disk is the natural home for full-screen GR.)

### High-resolution graphics - deferred

HGR (280×192) is not shipped in the current language. The Apple II display
reads high-resolution pages from main RAM at `$2000-$3FFF` / `$4000-$5FFF`,
which collides with the resident interpreter image and data structures. GR
mode (40×48, 16 colors) remains the shipped graphics surface because it uses
the text page (`$0400-$07FF`) and does not conflict with the interpreter.

The deferred design space lives in `ROADMAP-MAYBE.md`: likely //e-aux-only,
after a spike proves the interpreter, overlay park, and framebuffer can
co-exist and that aux/main bank switching is tolerable.

### Memory access

Direct memory reads and writes give access to soft switches (speaker,
paddles, shift-key, 80-col, etc.), the zero-page, ROM entry points, and
anything else in the Apple II address space.

| SwiftII                                   | Applesoft          | Notes                              |
|-------------------------------------------|--------------------|------------------------------------|
| `peek(_ addr: Int) -> Int`                | `PEEK(addr)`       | read byte at address 0–65535; returns 0–255. **Extras**. Host returns 0 |
| `poke(_ addr: Int, _ value: Int)`         | `POKE addr, val`   | write byte (0–255) to address. **Extras**. Host no-op |

**Status**: `peek` / `poke` are Extras builtins ($19 / $1A), alongside
`home()` ($18). They land via the cold extras path rather than MAIN - XLC
on SWIFTSAT, aux copy-down on SWIFTAUX, normal CODE in Family B / host -
because the lite binaries cannot absorb the parser and worker cost. See
design doc 012. On the host, `peek` returns 0 and `poke` is a no-op (no
raw-memory access) so the test suite stays deterministic; on Apple II
extras / Family B builds both touch main RAM directly.

**Lite/extras consistency**: the intent is for every
platform built-in to be available on both the lite binary
(`SWIFTIIP`/`SWIFTIIE`) and the extras binaries. The lite budget is too tight
for that, so they land on the same extras-only path as the other cold
builtins. `home` / `peek` / `poke` are therefore **absent on lite**;
calling them there is a compile error (`undeclared name`), exactly like
`asc`/`chr`/`Int(s)`. A future lite budget sweep or `OP_CALL_BUILTIN`
restructure could promote the primitives into MAIN for all binaries.

Common idioms (Extras / Family B / host):

```swift
// Click the speaker once
poke(49200, 0)                     // POKE 49200, 0  ($C030)

// Read paddle 0 (returns 0–255 after a delay)
let p = peek(49250)                // PEEK($C062) - wait for paddle

// Position cursor at column 5, row 10
htab(5); vtab(10)
```

### Timing

| SwiftII                  | Applesoft         | Notes                              |
|--------------------------|-------------------|------------------------------------|
| `wait(_ ms: Int)`        | `FOR…NEXT` delay  | busy-wait roughly `ms` milliseconds, then continue. A **Family B program builtin**: ships only on the Compiler + Runner (II+ and //e) + host. No REPL has it - pace with a counted loop there. |

`wait()` loops the monitor ROM `WAIT` routine (`$FCA8`) with a constant
baked in for the classic ~1.0205 MHz CPU clock - no runtime calibration,
because every original Apple II runs the 6502 at that fixed speed, so a
cycle count *is* a wall-clock time. The delay is **approximate** (a few
percent long from loop overhead; an accelerator card or IIgs fast mode
runs it short). On the host it is a no-op so test output stays
deterministic.

```swift
for i in 1...3 {
  print("tick")
  wait(500)                          // ~half a second between ticks
}
```

**Granularity - pacing, not sound.** `wait()` is millisecond-grained, for
human-visible pacing (animation frames, "press a key" rhythm, slowing a
loop). It is **not** a sound primitive. Apple II tones are made by toggling
the speaker every *half-period* - tens to hundreds of microseconds for
audible pitches (500 µs at 1 kHz, 100 µs at 5 kHz) - which is ~1000× finer
than a millisecond. And even a microsecond-grained `wait()` couldn't do it:
the per-call interpreter overhead (bytecode fetch + dispatch, tens of µs and
jittery) swamps the half-period, so the toggle loop must run as one native
counted routine. That is the `tone()` builtin (**Sound**, below), not a finer
`wait()`. (The hardware floor is one 6502 cycle, ~1 µs - nanosecond units
would be meaningless.) Applesoft was the same: no `SOUND`/`TONE` command, only
the fixed bell `PRINT CHR$(7)`; real tones were hand-rolled in assembly. For a
single click, `poke(49200, 0)` ($C030) is available on the extras binaries.

**Scope**: `wait()` is a **Family B program builtin** - write it in a
`.swift`, compile on the II+ or //e Compiler, run the `.swb` on the matching
Runner. It does **not** ship on any REPL (interactive or extras), and host
gets it for tests. The reasoning settled in two steps: a delay is for
*compiled programs*, not an interactive prompt; and no REPL has the
headroom anyway. The lite REPLs (SWIFTIIP/SWIFTIIE) have no platform table
at all. SWIFTSAT can't take it structurally - `wait()` must use the standard
monitor ROM `WAIT` (`$FCA8`), which runs only from MAIN (the ROM is banked
out under SWIFTSAT's Saturn XLC bank), and SWIFTSAT's MAIN is full at 5 B;
funding it would mean dropping ~4 platform builtins, three of which
(`hlin`/`vlin`/`scrn`) the shipped graphics demos use. SWIFTAUX *could* have
carried it (it has room), but a delay earns its place in programs, not at a
prompt, so it's left a clean Family-B-only feature. The Family B Runners
paid 86 B of BSS for the dispatch, recovered by a 128 B runtime-heap trim
each (max program *size* unchanged). At a REPL, use a counted loop for rough
pacing.
`exit()` and `heapAvailable()` were considered alongside it but dropped -
they duplicate the `:quit` and `:mem` REPL meta-commands.

### Sound

| SwiftII                                  | Applesoft        | Notes                              |
|------------------------------------------|------------------|------------------------------------|
| `tone(_ halfPeriod: Int, _ cycles: Int)` | (hand-rolled asm)| square-wave speaker tone. `halfPeriod` sets the delay between speaker toggles (pitch - larger is lower), `cycles` the number of full periods (duration). Blocks for the whole tone. Same **Family B program builtin** scope as `wait()`. |

`tone()` toggles the 1-bit speaker soft switch (`$C030`) in a counted loop:
each access flips the cone, so a full square-wave period is two toggles a
`halfPeriod` delay apart. Tune pitch by ear - larger `halfPeriod` is a lower
note; a rough audible range is `halfPeriod` in the low tens. Like `wait()`,
the delay is **approximate** (stable for a given `halfPeriod`, but a cc65
counted loop rather than a cycle-calibrated routine, and short on accelerator
cards / IIgs fast mode). Unlike `wait()` it touches no ROM, so it needs no
bank juggling. On the host it is a no-op so test output stays deterministic.

```swift
for note in [40, 30, 20] {
  tone(note, 200)                    // three rising blips
}
```

**Scope**: `tone()` is a **Family B program builtin** with exactly the same
residency as `wait()` - the Compiler (II+ and //e) recognizes it, the matching
Runner sounds it, and the host stubs it for tests. **No REPL ships it.** A tone
voices a *program*, not an interactive prompt, and the extras REPLs are kept
symmetric: SWIFTSAT (II+ Saturn) has only **5 B** of MAIN free - not enough for
even the recognizer row, whose toggle-loop worker would otherwise fit its XLC
bank - so giving it to SWIFTAUX (//e aux) alone would diverge the two extras
REPLs for one machine. The Family B Runners paid ~200 B of BSS for the dispatch
(`tone()` takes two args and the worker is a real toggle loop, heavier than
`wait()`'s tiny ROM-WAIT wrap), recovered by a 256 B runtime-heap trim each;
the tighter //e Compiler also took a 32 B C-stack-reserve trim. Max program
*size* is unchanged. At a REPL, there is no tone - the speaker click
`poke(49200, 0)` is the nearest extras-binary primitive.

### Math and string convenience builtins (Family B)

| SwiftII                              | Applesoft   | Notes                              |
|--------------------------------------|-------------|------------------------------------|
| `abs(_ x: Int) -> Int`               | `ABS(x)`    | absolute value. Real Swift.        |
| `sgn(_ x: Int) -> Int`               | `SGN(x)`    | `-1` / `0` / `1` by sign. BASIC-flavoured free function (Swift writes `x.signum()`). |
| `s.hasPrefix(_ t: String) -> Bool`   | -           | does `s` start with `t`? Empty `t` is always `true`. Real Swift. |
| `s.hasSuffix(_ t: String) -> Bool`   | -           | does `s` end with `t`? Empty `t` is always `true`. Real Swift. |

```swift
print(abs(-7))                       // 7
print(sgn(-3))                       // -1
print("hello.swift".hasSuffix(".swift"))   // true
print("hello".hasPrefix("he"))             // true
```

**Scope**: all four are **Family B program builtins** - the Compiler (II+ and
//e) recognizes them, the matching Runner executes them, and the host runs
them for tests. **No REPL ships them.** They are pure computation (no
hardware), so unlike `wait()`/`tone()` nothing *structurally* bars them from
the REPLs - the wall is budget. The most constrained REPL, the II+ lite
`SWIFTIIP`, is built with no feature flags and has no XLC bank to offload to;
`abs`/`sgn` alone overflowed its MAIN by ~280 B, and its only cuttable code is
genuine language features more useful at a prompt than `abs`. So, like
`wait()`/`tone()`, they stayed Family-B-only rather than diverge the REPLs or
sacrifice a feature. `hasPrefix`/`hasSuffix` additionally reverse a scope call
that dropped them twice as "text-parsing, not the target use case" (design doc
004) - they ship now because the Family B Compiler/Runner has the headroom the
REPLs lack, and they fold into the existing array-method (`.contains`)
recognizer almost for free. The Family B Runners paid ~514 B of BSS for all
four dispatchers (cc65's int16 and byte-compare codegen is fat), recovered by a
640 B runtime-heap trim each - a runtime-data capacity trade; max program
*size* is unchanged. The Compiler paid a 224 B C-stack-reserve trim. `sgn(x)`
is spelled BASIC-style on purpose: Swift has no free `sgn`, only `x.signum()`,
whose `.member` parse path is the heavy one - the cheap free-function form is
what fits, and it pairs with `abs(x)` (which *is* real Swift).

### File I/O (Family B Runner only)

Programs run by the **Family B Runner** can read and write disk files - the
Runner is a MAIN-only binary with ProDOS MLI intact, so it has real file
access (the Family A REPL does not: its language card holds the interpreter,
not MLI). Provided via the on-disk Compiler/Runner toolchain (design doc 015).

| SwiftII                                       | Notes                              |
|-----------------------------------------------|------------------------------------|
| `readFile(_ path: String) -> String?`         | whole file as a `String`, or `nil` if it can't be opened. Capped at 512 B/call (`USERFILE_READ_CAP`). |
| `writeFile(_ path: String, _ s: String) -> Bool` | create/truncate `path` and write `s`; `true` on success. |
| `appendFile(_ path: String, _ s: String) -> Bool` | append `s` to `path`, creating it if absent; `true` on success. |
| `deleteFile(_ path: String) -> Bool`          | delete a file (or empty directory); `true` on success. |
| `renameFile(_ old: String, _ new: String) -> Bool` | rename/move within the volume; `true` on success. |
| `fileExists(_ path: String) -> Bool`          | `true` if the path names an existing file or directory. |
| `createDirectory(_ path: String) -> Bool`     | create a directory; `true` on success. |
| `deleteDirectory(_ path: String) -> Bool`     | delete an **empty** directory (alias of `deleteFile`); `true` on success. |
| `listDirectory(_ path: String) -> [String]`   | names of the directory's entries (files and subdirectories), uppercase; empty array if it can't be opened. |

```swift
if let cfg = readFile("NOTES.TXT") {
  print(cfg)
}
let ok = writeFile("OUT.TXT", "hello from SwiftII\n")
_ = appendFile("LOG.TXT", "another line\n")

if !fileExists("DATA") { _ = createDirectory("DATA") }
let names = listDirectory("DATA")
var i = 0
while i < names.count {        // direct `for x in array` isn't in v1
  print(names[i])
  i = i + 1
}
```

The whole-file builtins (`readFile`/`writeFile`/`appendFile`) and the
metadata/directory builtins (`deleteFile`/`renameFile`/`fileExists`/
`createDirectory`/`deleteDirectory`/`listDirectory`) all landed via the
Family B Compiler/Runner toolchain (design docs 015 + 017).

**Family B dialect fork.** These are recognised only by the standalone
Compiler (gated `WITH_SWB`), so a program using them **compiles on a Family B
compiler disk** and **errors as an undeclared name in the Family A REPL** -
the deliberate per-feature split UCSD Pascal also had (the REPL has no MLI for
files anyway). On the host the same code path uses stdio/POSIX so the
round-trip is testable. Paths are partial (resolved against the ProDOS prefix)
or absolute, upper-cased (ProDOS is case-folding).

---

## Implementation limits

These are not language-design choices - they are numeric caps
imposed by the Apple II BSS budget. There are two tiers (design doc 016):
the **Family A REPL** (the four combined
interpreters, staged-source runs) and the **Family B Compiler/Runner**
toolchain, which streams source from disk and raises the table caps
via `-D` overrides (Makefile `COMPILER_DEFS`/`RUNNER_DEFS`).

| Limit                              | Family A REPL | Family B Compiler/Runner |
|------------------------------------|---------------|--------------------------|
| Identifier length (longer = error) | 11 chars      | 11 chars                 |
| Global variables (per program)     | 32            | 48                       |
| Functions (per program)            | 16            | 24                       |
| Locals per function                | 16            | 16                       |
| Function call depth (recursion)    | 4             | 4                        |
| Source file size                   | 2 KB (staged) | **disk-bounded** (streamed through a 4 KB window; one statement must fit the window) |
| Compiled bytecode                  | 1 KB          | 1,834 B flat Tier 1; ~64 KB Saturn / ~40 KB aux for function-heavy programs |
| Constant pool (compile-time heap)  | shares heap   | 768 B Tier 1; 704 B Saturn; 744 B aux |
| Runtime heap (strings/arrays)      | 2 KB          | Runner heap: II+ 2,136 B; //e 2,560 B; Saturn 1,792 B |
| String pool entries                | 16            | 16                       |
| VM stack depth                     | 32 slots      | 32 slots                 |

In practice a Family B Tier 1 program is capped by the **bytecode arena +
constant pool**, not source length; the Saturn and aux tiers lift total
bytecode for function-heavy programs but still cap the largest single
function / top-level body by their compile windows. The Family A caps are
sized so any demo
writable in Applesoft or Pascal on a stock Apple II Plus fits (see
`docs/contributing/design/004-demo-oriented-scope.md`).

Declaring an identifier longer than the 11-character cap is a
**compile-time error** (`name >11 chars`), not a silent
truncation - so `accountBalanceUSD` is rejected outright rather than
silently colliding with `accountBalanceEUR`. Typical Swift naming
(`position`, `velocity`, `playerScore`, `currentLevel`) fits
comfortably. See [Identifiers](#identifiers) above.

The limits are shared between host and Apple II builds - the
constants live in `src/common/config.h` (with `#ifndef` guards so the
Family B builds can raise theirs) - so host tests catch over-length
names and capacity overflows before they hit the cc65 build.

---

## Differences from Swift

These are deliberate. They are not bugs.

1. **Integers are 16-bit signed.** Swift's `Int` is 64-bit on modern
   platforms; ours fits in a register pair.
2. **Integers are the only number type** - there is no `Double` or
   `Float`; a `.` in a numeric literal is a compile error.
3. **No `Character` type.** Strings are byte sequences; iterating a
   string gives bytes (Tier 2), not graphemes.
4. **No Unicode.** ASCII source, ASCII strings.
5. **No first-class functions or closures.** Functions are top-level
   declarations only and are not values; closures are not supported
   (see item 4).
6. **No nested types.** No nested functions, no nested structs.
7. **No tuples.** `Void` return values are represented by a `nil`
   placeholder when a void call is evaluated as an expression; there is
   no `()` literal or general tuple syntax.
8. **No `where` clauses, no generics.**
9. **No `defer`.**
10. **No exceptions / error-handling syntax in Tier 1.** Runtime errors
    halt the program (or, in the REPL, return to the prompt).
11. **String interpolation only supports built-in types.** Custom types
    can't override string conversion.
12. **`switch` is not in Tier 1.** Use `if/else if`. `switch` is in
    Tier 2 with limited pattern support (literals and `_`).
13. **Compound assignment is limited to four operators.** `+=`, `-=`,
    `*=`, `/=` work. `%=`, `&=`, `|=`, `^=`, `<<=`, `>>=`
    are not provided (no bitwise operators in Tier 1 either).
14. **No Swift concurrency.** `actor`, `async`, `await`, `Sendable`,
    `isolated`/`nonisolated`, `sending`, structured tasks, and the rest
    of Swift 5.5–6 concurrency are out of scope. The Apple II is
    single-threaded with no preemption; there is nothing to concur
    with. These keywords are not reserved (Swift treats them as
    contextual), so user code may use them as identifiers.
15. **No ownership / noncopyable types.** `~Copyable`, `~Escapable`,
    and the `consuming` / `borrowing` parameter modifiers (Swift 5.9+)
    are not supported. SwiftII uses reference counting for heap values
    and value semantics for immediates; the move-only model adds
    compiler complexity we don't need.
16. **No macros.** `#`-prefixed freestanding macros and `@`-prefixed
    attached macros (Swift 5.9+) are not parsed or expanded. The
    standard `#file`, `#line`, `#function` magic literals are also
    omitted.
17. **No typed throws, no error handling.** Swift 6's `throws(E)` and
    the surrounding `try`/`catch`/`Result` machinery are absent (see
    point 10). Runtime errors are unrecoverable.
18. **No raw string literals.** `#"..."#` and the `#`-delimited
    interpolation form are not recognised. Use the standard `"..."`
    form with `\\` escapes.
19. **No closures.** Swift (and Embedded Swift) support non-escaping
    closures; SwiftII does not. Functions are top-level declarations
    only, not first-class values. The upvalue-capture machinery costs
    3–4 KB of cc65 code - too expensive given the overall binary
    budget.
20. **No `Dictionary`.** Swift (and Embedded Swift) provide
    `Dictionary<K, V>`; SwiftII does not. A correct hash-table
    implementation costs ~1.5–2 KB of code and a small dictionary
    burns ~200 bytes of the 6 KB heap. Use parallel arrays or a
    linear-scan array of structs instead.

---

## Grammar (EBNF, abbreviated)

This is descriptive, not the canonical grammar. The compiler is a
hand-written single-pass Pratt parser; this section exists for
orientation.

```
program     ::= top_level*
top_level   ::= func_decl | statement

func_decl   ::= 'func' IDENT '(' params? ')' ('->' type)? block
params      ::= param (',' param)*
param       ::= label? IDENT ':' type
label       ::= IDENT | '_'

statement   ::= var_decl
              | assignment
              | if_stmt
              | while_stmt
              | for_stmt
              | switch_stmt
              | return_stmt
              | break_stmt
              | expr_stmt
              | block

var_decl    ::= ('let' | 'var') IDENT (':' type)? ('=' expression)?
assignment  ::= lvalue ('=' | '+=' | '-=' | '*=' | '/=') expression
if_stmt     ::= 'if' (if_let | expression) block ('else' (if_stmt | block))?
if_let      ::= 'let' IDENT ('=' expression)?
while_stmt  ::= 'while' expression block
for_stmt    ::= 'for' IDENT 'in' expression block
switch_stmt ::= 'switch' expression '{' switch_clause* default_clause? '}'
switch_clause ::= 'case' literal (',' literal)* ':' statement*
default_clause ::= 'default' ':' statement*
return_stmt ::= 'return' expression?
break_stmt  ::= 'break'
block       ::= '{' statement* '}'

expression  ::= (Pratt parser; see compiler/pratt.c)
type        ::= IDENT '?'?        // Int, Bool, String, ...
              | '[' type ']'      // [Int], [String], ...

// Lexical (informally):
INT_LIT     ::= dec_digits

dec_digits  ::= [0-9]+
```
