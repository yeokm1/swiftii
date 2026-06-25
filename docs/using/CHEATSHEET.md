# SwiftII cheat sheet

One-page quick reference. For signatures, platform notes, and examples see
the [API reference](API.md); for full semantics see [LANGUAGE.md](LANGUAGE.md).

**Availability key** - every built-in is one of:
`Core` (all binaries) · `Extras` (`SWIFTSAT`/`SWIFTAUX` + Family B; **not**
the lite REPLs) · `Family B` (compiled programs only - **no REPL**).

---

## Types & values

```swift
Int  Bool  String  [T]  T?  Void      // 16-bit Int, ASCII String, arrays, optionals, no-return
let x = 5            // immutable, inferred Int
var s = "hi"         // mutable
let y: Int? = nil    // optional needs annotation when bare nil
[1, 2, 3]            // [Int]   ("two" mixed in → error: homogeneous only)
true   false   nil   "a\(x)b"  // literals + interpolation; Int literals are decimal
```

## Operators

```text
+  -  *  /  %            arithmetic (+ concatenates String)
== != <  <= >  >=        comparison   (String ==  is identity, not content!)
&& || !                  logical (short-circuit)
=  += -= *= /=           assignment (compound = x = x <op> y)
??   x!                  nil-coalesce · force-unwrap
a..<b   a...b            half-open · closed range
```

## Control flow

```swift
if c { } else if d { } else { }        // condition must be Bool
if let v = opt { }                     // unwrap;  if let opt { } shorthand
while c { }
for i in 0..<n { }                     // range only (Core)
break                                  // continue is reserved, not implemented
return v                               // explicit on every non-void path

for item in xs { }                     // Family B - direct array iteration
switch n { case 0: …; case 1,2: …; default: … }   // Family B - Int/Bool, no fall-through
```

## Functions

```swift
func square(_ x: Int) -> Int { return x * x }   // _ = no call-site label → square(5)
func greet(person: String) -> String { return "Hi \(person)" }
func tick() { print("tick") }                   // void
// ≤16 functions, ≤16 locals each, recursion depth 4 (Family B: 24 funcs)
```

## Strings

```swift
"a" + "b"            s += "x"          // concat / append (immutable → new string)
s.count   s.isEmpty                    // Core
s.hasPrefix(t)   s.hasSuffix(t)        // Family B → Bool
```

## Arrays

```swift
var xs = [1, 2, 3]
xs[0]        xs[1] = 9                  // read / write (var only)
xs.count     xs.isEmpty     xs.append(x)            // Core
xs.removeLast()   xs.removeAll()   xs.contains(v)   // Extras
for i in 0..<xs.count { print(xs[i]) }              // index walk (Core)
```

## Conversions & math - global functions

```swift
print(v)        print(v, terminator: sep)    // Core (terminator must be String)
readLine()                                    // Core → String?
min(a, b)   max(a, b)                          // Core → Int
String(n)                                      // Core: Int → String   (][+: 'String)
Int(s)                                         // Extras: String → Int? (][+: 'Int)
asc("A") // 65       chr(66) // "B"            // Extras
abs(x)   sgn(x)                                // Family B → Int (-1/0/1)
random(in: 1...6)    random(in: 0..<2)         // Family B → Int
```

## Screen & graphics - Extras

```swift
home()                                  // clear text screen
htab(col)   vtab(row)                   // cursor: col 1–40/80, row 1–24
text()      text80()                    // 40-col / 80-col  (text/text80 also on SWIFTIIE lite)

gr()        grFull()                    // mixed 40×40+text / full 40×48   (][+: gr'full)
color(0…15)                             // set color
plot(x, y)                              // x 0–39, y 0–39 (gr) / 0–47 (grFull)
hlin(x1, x2, y)   vlin(y1, y2, x)       // runs: endpoints first, then fixed coord
scrn(x, y)                              // → color at (x, y)
```

## Memory - Extras

```swift
peek(addr)            // → byte 0–255   (addr 0–65535)
poke(addr, value)     // write byte
poke(49200, 0)        // $C030: click the speaker
```

## Timing & sound - Family B

```swift
wait(ms)                       // busy-wait ~ms milliseconds (pacing, not sound)
tone(halfPeriod, cycles)       // speaker tone: larger halfPeriod = lower pitch
```

## File & directory I/O - Family B

```swift
readFile(path)               // → String?  (≤512 B/call)
writeFile(path, s)           // → Bool  (create/truncate)
appendFile(path, s)          // → Bool
deleteFile(path)             // → Bool   (renameFile(old, new) → Bool)
fileExists(path)             // → Bool
createDirectory(path)        // → Bool   (deleteDirectory(path) → Bool, empty only)
listDirectory(path)          // → [String]  (uppercase names)
```

---

## Typing on a ][+ (uppercase-only keyboard)

The //e types Swift normally. On a ][+ / original ][ an input layer maps
keystrokes to canonical lowercase ASCII:

```text
LET X       → let x         letters auto-lowercase
'INT        → Int           ' = next letter uppercase   ('readLine 'String gr'full)
''MAX       → MAX           '' = uppercase run to next non-letter
Ctrl-W      → _
<%  %>      → {  }           block braces
<:  :>      → [  ]           array brackets / subscripts
??/         → \              interpolation:  "x=??/(x)"  → "x=\(x)"
??!         → |              reserved, not used in Tier 1
```

In a string, `'` between letters is literal (`"DON'T"` → `"don't"`); after a
non-letter it's still a case marker.

---

### Binaries

`SWIFTIIP` ][+ lite · `SWIFTIIE` //e lite · `SWIFTSAT` ][+ Saturn extras ·
`SWIFTAUX` //e aux extras · **Family B** = `COMPILER`+`RUNNER` (`.swift`→`.swb`,
biggest dialect, only one with file I/O).

Calling a built-in a binary doesn't carry is a **compile error**, not a no-op.
