# API.md - SwiftII developer API reference

A lookup-oriented catalog of everything you can *call* or *write* in a
SwiftII program: the types, the operators, the control-flow forms, and
every built-in function, method, and property - each with its **signature**,
its **platform availability**, and a short example.

This is the reference you reach for while writing Swift. For a one-screen
summary see the [cheat sheet](CHEATSHEET.md); for the full semantics and the
*why* behind each decision, see the language spec [LANGUAGE.md](LANGUAGE.md).
Where the two disagree, [LANGUAGE.md](LANGUAGE.md) is authoritative.

---

## How to read this document

### The binaries

SwiftII ships as several binaries. A feature is available on a binary or it
is not - calling a built-in that a binary doesn't carry is a compile error
(`undeclared name`), not a runtime no-op.

| Binary | Machine | Role |
|--------|---------|------|
| `SWIFTIIP` | Apple ][ / ][+ | **lite** REPL - core language |
| `SWIFTIIE` | //e | **lite** REPL - core language (+ `text`/`text80`) |
| `SWIFTSAT` | ][+ with Saturn 128K | **extras** REPL - core + graphics/memory + speaker click |
| `SWIFTAUX` | //e with 64K aux | **extras** REPL - core + graphics/memory + 80-col |
| **Family B** | `COMPILER` + `RUNNER` | compiles `.swift` → `.swb`, runs the program from disk |
| host | your Mac | `swbc` / test harness - stubs Apple II hardware |

"Family B" means a program you **compile to a `.swb` and run** with the
on-disk Compiler/Runner toolchain - not an interactive REPL. It is the
largest dialect, streams source from disk, has tiered bytecode limits, and is
the only one with file I/O.

### The availability tags

Every entry below carries one tag:

| Tag | Available on | Notes |
|-----|--------------|-------|
| **Core** | every binary + Family B + host | the portable subset; runs anywhere |
| **Extras** | `SWIFTSAT`, `SWIFTAUX`, Family B, host | **not** the lite REPLs (`SWIFTIIP`/`SWIFTIIE`) |
| **Family B** | `COMPILER`/`RUNNER` programs + host | **no REPL** has these - compiled programs only |

> **Why a built-in is "Extras" or "Family B."** The whole language lives
> against the Apple II's 64 KB ceiling. The lite REPLs are full; extras
> functions live in a separate code bank (Saturn / aux) the lite binaries
> don't have. Family-B-only functions either need ROM/MLI that a REPL banks
> out, or simply earn their place in a *program* rather than at a prompt.
> The Family B Compiler/Runner recognises and executes the
> **entire** Extras surface too - so a graphics program compiled on a Family
> B disk runs on a bare 64K machine.

### Availability at a glance

The tag tells you the *class*; this matrix spells out the **exact binary**.
✓ = present, — = compile error (`undeclared name`). The host column tracks
Family B (it carries the whole surface, stubbing hardware).

| Feature group | `SWIFTIIP`<br>II+ lite | `SWIFTIIE`<br>//e lite | `SWIFTSAT`<br>II+ Saturn | `SWIFTAUX`<br>//e aux | Family B<br>(+ host) |
|---|:---:|:---:|:---:|:---:|:---:|
| Core language — types, operators, `if`/`while`/`for i in a..<b`, functions | ✓ | ✓ | ✓ | ✓ | ✓ |
| `print` `readLine` `min` `max` `String(_:)` · `.count` `.isEmpty` · array `.append`/subscript | ✓ | ✓ | ✓ | ✓ | ✓ |
| `text()` · `text80()` | — | ✓ | ✓ | ✓ | ✓ |
| `Int(_:)` `asc` `chr` · array `.removeLast` `.removeAll` `.contains` | — | — | ✓ | ✓ | ✓ |
| Graphics `gr`…`scrn` · `home` `htab` `vtab` · `peek` `poke` | — | — | ✓ | ✓ | ✓ |
| `abs` `sgn` `random(in:)` · `.hasPrefix` `.hasSuffix` | — | — | — | — | ✓ |
| `for item in array` · `switch` | — | — | — | — | ✓ |
| `wait` `tone` · file & directory I/O (`readFile`…`listDirectory`) | — | — | — | — | ✓ |

The per-entry **Core / Extras / Family B** tag below each function is the
authoritative source; this matrix is the same information indexed by binary.
80-column text also needs the hardware (a //e, or Saturn + Videx for the
`SWIFTSAT` REPL path); see
[Screen & text](#screen--text).

### Typing the names on a ][+

Canonical `.swift` source is lowercase ASCII. The //e and later type Swift
normally. The uppercase-only ][+ (and original Apple ][) keyboard can't type
lowercase, `{ } [ ] \ _`, or `|`, so an **input layer** maps what you type to
canonical bytes:

| Type this | To get | Use |
|-----------|--------|-----|
| letters | auto-lowercased | `LET X` → `let x` |
| `'` *before a letter* | one uppercase letter | `'INT` → `Int`, `'READLINE` → `readLine` |
| `''` *before letters* | uppercase run (to next non-letter) | `''MAX` → `MAX` |
| `Ctrl-W` | `_` | `'STRING(N)` etc. |
| `<%`  `%>` | `{`  `}` | block braces |
| `<:`  `:>` | `[`  `]` | array brackets / subscripts |
| `??/` | `\` | string interpolation `\(…)` |
| `??!` | `\|` | reserved; not used in Tier 1 |

So a graphics call is typed `gr'full(...)`, an array literal `<:1, 2, 3:>`,
an interpolation `"x=??/(x)"`. Inside a string literal, `'` *between* two
letters is a literal apostrophe (`"DON'T"` → `"don't"`); after a non-letter
it's still a case marker. Full rules:
[design doc 003](../contributing/design/003-apple2-input-method.md).

---

## Contents

- [Types](#types)
- [Operators](#operators)
- [Control flow](#control-flow)
- [Declaring functions](#declaring-functions)
- [Global functions](#global-functions) - `print` `readLine` `min` `max` `abs` `sgn` `random` `String` `Int` `asc` `chr`
- [String members](#string-members) - `.count` `.isEmpty` `.hasPrefix` `.hasSuffix`
- [Array members](#array-members) - `.count` `.isEmpty` `.append` `.removeLast` `.removeAll` `.contains` subscript
- [Screen & text](#screen--text) - `home` `htab` `vtab` `text` `text80`
- [Low-res graphics](#low-res-graphics) - `gr` `grFull` `color` `plot` `hlin` `vlin` `scrn`
- [Memory](#memory) - `peek` `poke`
- [Timing & sound](#timing--sound) - `wait` `tone`
- [File & directory I/O](#file--directory-io) - `readFile` `writeFile` `appendFile` `deleteFile` `renameFile` `fileExists` `createDirectory` `deleteDirectory` `listDirectory`
- [Limits at a glance](#limits-at-a-glance)

---

## Types

**Core** - all types are available everywhere.

| Type | Meaning | Literal | Notes |
|------|---------|---------|-------|
| `Int` | 16-bit signed integer (−32768…32767) | `42`, `-7` | immediate (no heap); decimal literals only |
| `Bool` | `true` / `false` | `true` | immediate |
| `String` | immutable ASCII byte sequence | `"hi"`, `"a\(x)b"` | heap, ref-counted |
| `[T]` / `Array` | homogeneous, growable | `[1, 2, 3]` | heap, ref-counted |
| `T?` | optional of `T` (`Int?`, `String?`, …) | `nil` | immediate tag |
| `Void` | no-return function result | - | a void call yields a `nil` placeholder when evaluated |

```swift
let x = 5              // Int (inferred)
let s = "hi"           // String
let xs = [1, 2, 3]     // [Int]
let y: Int? = nil      // explicit annotation needed; bare nil is ambiguous
var n: Int             // OK, must be assigned before use
```

Arrays are homogeneous (`[1, "two"]` is a compile error). `String` and
`[T]` interpolate into strings via `\(...)`; so do `Int`, `Bool`, and any
`T?`. Full details: [LANGUAGE.md → Types](LANGUAGE.md#types).

### Optionals

```swift
let maybe: Int? = 5
if let x = maybe { print(x) }   // unwrap; 5
if let maybe { print(maybe) }   // Swift 5.7 shorthand
let n = maybe ?? 0              // nil-coalescing default
let f = maybe!                  // force-unwrap; runtime error if nil
```

---

## Operators

**Core.**

| Group | Operators |
|-------|-----------|
| Arithmetic | `+` `-` `*` `/` `%` (Int; `+` also concatenates `String`) |
| Comparison | `==` `!=` `<` `<=` `>` `>=` |
| Logical | `&&` `\|\|` `!` (`&&` / `\|\|` short-circuit) |
| Assignment | `=` `+=` `-=` `*=` `/=` (compound desugars to `x = x <op> y`) |
| Optional | `??` (coalesce) · `!` (force-unwrap, postfix) |
| Range | `a..<b` (half-open) · `a...b` (closed) |

`==` / `!=` on `String` compare **identity, not content** (see
[LANGUAGE.md](LANGUAGE.md#differences-from-swift)). `%=` and bitwise compound
assignments are not implemented. Full table:
[LANGUAGE.md → Operators](LANGUAGE.md#operators).

---

## Control flow

| Form | Availability | Notes |
|------|--------------|-------|
| `if` / `else if` / `else` | **Core** | condition must be `Bool` |
| `if let` (+ shorthand) | **Core** | unwrap an optional |
| `while` | **Core** | condition must be `Bool` |
| `for i in a..<b` / `a...b` | **Core** | iterate a **range** |
| `for item in array` | **Family B** | direct array iteration; REPLs use `for i in 0..<xs.count` |
| `break` | **Core** | exits innermost loop; `continue` is reserved, not implemented |
| `switch` | **Family B** | `Int`/`Bool` only, comma-grouped cases, no fall-through, no `String` switch |
| `return` / `return v` | **Core** | every non-void path must `return` (no implicit return) |

```swift
for i in 0..<items.count {       // index walk - works in every binary
    print(items[i])
}

// Family B only:
for item in items { print(item) }
switch n {
case 0:       print("zero")
case 1, 2, 3: print("small")     // comma-grouped
default:      print("other")
}
```

Full semantics: [LANGUAGE.md → Statements](LANGUAGE.md#statements).

---

## Declaring functions

**Core.**

```swift
func greet(person: String) -> String {
    return "Hello, \(person)!"
}

func tick() {                    // void return
    print("tick")
}

func square(_ x: Int) -> Int {   // _ = no argument label at the call site
    return x * x
}
```

- Up to **16 functions** per program (24 on Family B), **16 locals** each,
  recursion depth **4**.
- Value-returning functions need an explicit `return` on every path.
- Argument labels at the *call site* are not supported - declare params
  with `_` and call positionally: `square(5)`.

Details: [LANGUAGE.md → Functions](LANGUAGE.md#functions).

---

## Global functions

### `print(_ value)` · `print(_ value, terminator: String)` - **Core**

Print any interpolatable value, followed by a newline (or by `terminator`,
which must evaluate to a `String`).

```swift
print("Hello")
print("Enter name: ", terminator: "")   // inline prompt, no newline
print(score, terminator: " ")           // build a row of values
```

### `readLine() -> String?` - **Core**

Read one line from the keyboard; `nil` at EOF.

```swift
print("Name? ", terminator: "")
let name = readLine() ?? "anon"
```

### `min(_ a: Int, _ b: Int) -> Int` · `max(_ a: Int, _ b: Int) -> Int` - **Core**

```swift
print(min(3, 7))    // 3
print(max(-2, 5))   // 5
```

### `abs(_ x: Int) -> Int` - **Family B**

Absolute value (real Swift).

### `sgn(_ x: Int) -> Int` - **Family B**

Sign as `-1` / `0` / `1` (BASIC-flavoured; Swift writes `x.signum()`).

```swift
print(abs(-7))   // 7
print(sgn(-3))   // -1
```

### `random(in: a..<b) -> Int` · `random(in: a...b) -> Int` - **Family B**

Pseudo-random `Int` in a half-open or closed range. Empty/inverted range is
a runtime error. On Apple II it folds in keypress-timing entropy (draw
*after* an input for variety); on the host it is deterministic for
reproducible tests.

```swift
let die  = random(in: 1...6)   // 1…6 inclusive
let coin = random(in: 0..<2)   // 0 or 1
```

### `String(_ n: Int) -> String` - **Core**

Decimal text for an Int (same path as `\(n)` interpolation). On a ][+ type
`'String`.

### `Int(_ s: String) -> Int?` - **Extras**

Parse a decimal string (optional leading `+`/`-`) to an Int; `nil` on empty,
non-numeric, or out-of-range. On a ][+ type `'Int`.

```swift
if let n = Int(readLine() ?? "") {
    print(n * 2)
}
```

### `asc(_ s: String) -> Int` - **Extras**

ASCII value of the first byte (runtime error if `s` is empty). Like
Applesoft `ASC`.

### `chr(_ n: Int) -> String` - **Extras**

Single-character string for byte `n` (0–255); runtime error if out of range.
Inverse of `asc` - `chr(asc(s))` recovers the first character. Like
Applesoft `CHR$`.

```swift
print(asc("A"))     // 65
print(chr(66))      // "B"
```

---

## String members

Strings are **immutable**; `+` concatenates and `+=` appends (returns a new
string).

| Member | Returns | Availability |
|--------|---------|--------------|
| `s.count` | `Int` (byte count) | **Core** |
| `s.isEmpty` | `Bool` | **Core** |
| `s.hasPrefix(_ t: String)` | `Bool` - does `s` start with `t`? (empty `t` → `true`) | **Family B** |
| `s.hasSuffix(_ t: String)` | `Bool` - does `s` end with `t`? (empty `t` → `true`) | **Family B** |

```swift
let c = "foo" + "bar"               // "foobar"
print(c.count)                      // 6
print("hello.swift".hasSuffix(".swift"))   // true (Family B)
```

---

## Array members

```swift
var xs = [1, 2, 3]
let first = xs[0]      // subscript read (runtime error if out of bounds)
xs[1] = 9             // subscript write (var only; let → compile error)
```

| Member | Returns | Availability |
|--------|---------|--------------|
| `xs.count` | `Int` | **Core** |
| `xs.isEmpty` | `Bool` | **Core** |
| `xs.append(_ x)` | pushes `x` (statement form writes back in place) | **Core** |
| `xs[i]` / `xs[i] = v` | subscript read / write | **Core** |
| `xs.removeLast() -> Element` | removes & returns last (runtime error if empty) | **Extras** |
| `xs.removeAll()` | empties in place; returns the void-call `nil` placeholder | **Extras** |
| `xs.contains(_ v) -> Bool` | element equal to `v`? (value equality; identity for strings) | **Extras** |

`map`, `filter`, `for item in xs` (direct iteration is Family B) and other
sequence methods are not in v1. Walk by index: `for i in 0..<xs.count`.
Details: [LANGUAGE.md → Arrays](LANGUAGE.md#arrays).

---

## Screen & text

**Extras** (exception: `text()` / `text80()` are also on the `SWIFTIIE`
lite //e binary). On the host these are no-ops apart from range checks.

| Function | Applesoft | Description |
|----------|-----------|-------------|
| `home()` | `HOME` | clear text screen, cursor top-left |
| `htab(_ col: Int)` | `HTAB` | cursor to column 1–40 (1–80 in 80-col); runtime error if out of range |
| `vtab(_ row: Int)` | `VTAB` | cursor to row 1–24; runtime error if out of range |
| `text()` | `TEXT` | return to 40-col text + clear (on //e also reverts 80→40) |
| `text80()` | `PR#3` | switch to 80-col text where a card is present; no-op otherwise |

80-column text needs a supported device: //e firmware (`SWIFTIIE` /
`SWIFTAUX`), Saturn + Videx for the `SWIFTSAT` REPL path, or Videx on the
Family B Runner. Both //e binaries come up in 80 columns when a card is
detected.

```swift
home()
vtab(10); htab(5)
print("centered-ish")
```

---

## Low-res graphics

**Extras.** 40×48 grid, 16 colors, on the text page. (HGR is not shipped.)
Host: no-ops apart from range checks.

| Function | Applesoft | Description |
|----------|-----------|-------------|
| `gr()` | `GR` | enter **mixed** GR (40×40 + 4-line text window); `plot` y 0–39 |
| `grFull()` | `GR` + full-screen | enter **full-screen** GR (40×48, no text window); `plot` y 0–47 |
| `text()` | `TEXT` | back to 40-col text + clear |
| `color(_ n: Int)` | `COLOR=` | set color 0–15; runtime error if out of range |
| `plot(_ x: Int, _ y: Int)` | `PLOT` | plot one block (x 0–39; y per mode) |
| `hlin(_ x1: Int, _ x2: Int, _ y: Int)` | `HLIN x1,x2 AT y` | horizontal run at row `y` (endpoints either order) |
| `vlin(_ y1: Int, _ y2: Int, _ x: Int)` | `VLIN y1,y2 AT x` | vertical run at column `x` |
| `scrn(_ x: Int, _ y: Int) -> Int` | `SCRN(x,y)` | read color 0–15 at `(x, y)` (host returns 0) |

`hlin`/`vlin` take the two endpoints **first**, then the fixed coordinate
(all positional - no `at:` label). On a ][+ type `gr'full` (capital F).

```swift
gr()
color(9)
hlin(0, 39, 20)        // a red line across row 20
plot(20, 10)
let _ = readLine()     // hold the screen until Enter
```

To view a `grFull()` screen interactively, pause before the prompt returns
(`let _ = readLine()`) - the full-screen mode has no text window, so the
prompt would draw over the art.

In **mixed** `gr()` mode, `print` output goes to the 4-line text window
(rows 20-23) and scrolls within it - the 40×40 picture above stays put, so a
multi-scene program can narrate each scene without disturbing the graphics.
(Switch to `grFull()` only when you want the whole screen for art and no text.)

---

## Memory

**Extras.** Host: `peek` returns 0, `poke` is a no-op.

| Function | Applesoft | Description |
|----------|-----------|-------------|
| `peek(_ addr: Int) -> Int` | `PEEK` | read byte at 0–65535; returns 0–255 |
| `poke(_ addr: Int, _ value: Int)` | `POKE` | write byte 0–255 to an address |

```swift
poke(49200, 0)              // $C030 - click the speaker once
let paddle = peek(49250)    // $C062 - read paddle 0
```

---

## Timing & sound

**Family B** - no REPL ships these (a delay or tone earns its place in a
program, not at a prompt). Host: no-ops so test output stays deterministic.

| Function | Description |
|----------|-------------|
| `wait(_ ms: Int)` | busy-wait roughly `ms` milliseconds, then continue (ROM `WAIT $FCA8`; ~few % long; short on accelerators / IIgs fast mode) |
| `tone(_ halfPeriod: Int, _ cycles: Int)` | square-wave speaker tone: `halfPeriod` sets pitch (larger = lower), `cycles` sets duration. Blocks for the whole tone |

`wait` is for human-visible **pacing**, not sound (it is millisecond-grained;
audible tones need microsecond toggles - that's `tone`). At a REPL there is
no `wait`/`tone`; pace with a counted loop, and `poke(49200, 0)` clicks the
speaker once on the extras binaries.

```swift
for i in 1...3 {
    print("tick")
    wait(500)               // ~half a second
}
for note in [40, 30, 20] {
    tone(note, 200)         // three rising blips
}
```

---

## File & directory I/O

**Family B** - no REPL ships these (the Runner keeps ProDOS MLI in memory;
a REPL's language card holds the interpreter instead, so it can't do file
access). Paths are partial (against the ProDOS prefix) or absolute, and
upper-cased. Host uses
stdio/POSIX so round-trips are testable.

| Function | Description |
|----------|-------------|
| `readFile(_ path: String) -> String?` | whole file as a `String` (≤ 512 B/call), or `nil` if it can't open |
| `writeFile(_ path: String, _ s: String) -> Bool` | create/truncate and write; `true` on success |
| `appendFile(_ path: String, _ s: String) -> Bool` | append (creating if absent); `true` on success |
| `deleteFile(_ path: String) -> Bool` | delete a file (or empty dir); `true` on success |
| `renameFile(_ old: String, _ new: String) -> Bool` | rename/move within the volume |
| `fileExists(_ path: String) -> Bool` | `true` if the path exists (file or directory) |
| `createDirectory(_ path: String) -> Bool` | create a directory |
| `deleteDirectory(_ path: String) -> Bool` | delete an **empty** directory (alias of `deleteFile`) |
| `listDirectory(_ path: String) -> [String]` | entry names (uppercase); empty array if it can't open |

```swift
if let txt = readFile("NOTES.TXT") { print(txt) }
let ok = writeFile("OUT.TXT", "hello from SwiftII\n")
_ = appendFile("LOG.TXT", "another line\n")

if !fileExists("DATA") { _ = createDirectory("DATA") }
let names = listDirectory("DATA")
for i in 0..<names.count { print(names[i]) }
```

---

## Limits at a glance

| Limit | Family A REPL | Family B Compiler/Runner |
|-------|--------------:|-------------------------:|
| Identifier length | 11 chars | 11 chars |
| Global variables | 32 | 48 |
| Functions | 16 | 24 |
| Locals per function | 16 | 16 |
| Recursion depth | 4 | 4 |
| Compiled bytecode | 1 KB | 1,834 B flat Tier 1; larger on Saturn / aux for function-heavy programs |
| Runtime heap (strings/arrays) | 2 KB | II+ 2,136 B; //e 2,560 B; Saturn 1,792 B |
| String pool entries | 16 | 16 |
| VM stack depth | 32 slots | 32 slots |

Declaring an identifier longer than the cap is a **compile-time error**
(`name longer than 11 chars`), not a silent truncation. Full table and rationale:
[LANGUAGE.md → Implementation limits](LANGUAGE.md#implementation-limits).

---

## See also

- [CHEATSHEET.md](CHEATSHEET.md) - the one-page quick reference
- [LANGUAGE.md](LANGUAGE.md) - the authoritative language spec (semantics, grammar, differences from Swift)
- [TUTORIAL.md](TUTORIAL.md) - hands-on user guide (boot, REPL, write & run a program)
- [FEATURES.md](FEATURES.md) - every feature and what it costs (disk + memory)
