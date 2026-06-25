# FEATURES.md - what SwiftII ships, and what each feature costs

A reference table of every shipped language / runtime / platform / editor
/ launcher / Family-B feature, with the two costs that matter on a 64 KB
Apple II: **disk space** (bytes of binary / on-disk footprint) and
**memory** (BSS / heap / RAM).

The point is to see, at a glance, what each feature buys and what it
costs. The whole project lives against the **ProDOS single-file ceiling of
40,704 bytes** (`$2000`–`$BEFF`), so a feature's marginal byte cost is
often the reason it ships on one binary and not another.

> **Sources.** Top-level footprints are exact, from `make size`.
> Per-feature marginal costs are the documented numbers from
> [`ROADMAP.md`](../contributing/ROADMAP.md), [`LESSONS.md`](../contributing/LESSONS.md), and the numbered
> [`design/`](../contributing/design) records; where a number is approximate the source
> recorded it that way (cc65 codegen varies). Re-run `make size` to refresh
> the footprints.

---

## 1. Binary footprints (disk space)

Exact, from `make size` (snapshot: v1.0.0, 2026-06-23). Budget is the
40,704-byte ProDOS ceiling.

| Binary | Target | Bytes | Headroom | On-disk role |
|--------|--------|------:|---------:|--------------|
| `SWIFTIIP.SYSTEM` | II+ lite | 40,692 | 12 | REPL, core language |
| `SWIFTSAT` MAIN | II+ Saturn | 40,702 | 2 | REPL extras (hot ops) |
| `SWIFTSAT` XLC | II+ Saturn | 7,602 | 4,686 (in 12 KB LC) | cold bodies + REPL cursor in Saturn bank 1 |
| `SWIFTSAT` total | II+ Saturn | 48,308 | - | MAIN + XLC overlay |
| `SWIFTIIE.SYSTEM` | //e lite | 40,184 | 520 | REPL, core language |
| `SWIFTAUX` MAIN | //e aux | 39,927 | 777 | REPL extras (hot ops) |
| `SWIFTAUX` park | //e aux | 7,522 | - | cold bodies copied into aux |
| `SWIFTAUX` total | //e aux | 47,553 | - | MAIN + aux park |
| `SWIFTII.SYSTEM` | II+ launcher | 35,311 | 5,393 | boot menu + file UI + editor |
| `SWIFTII.SYSTEM` | //e launcher | 34,737 | 5,967 | boot menu + file UI + editor |
| `DEBUG.SYSTEM` | diagnostic | 3,784 | - | 3-page hardware readout |
| `COMPILER.SYSTEM` | Family B | 35,032 | 5,672 | `.swift` → `.swb` |
| `COMPILER.SYSTEM` | Family B //e | 35,704 | 5,000 | `.swift` → `.swb` (aux-paged) |
| `COMPILER.SYSTEM` | Family B Saturn | 36,119 | 4,585 | `.swift` → `.swb` (Saturn-paged) |
| `RUNNER.SYSTEM` | Family B II+ | 31,940 | 8,764 | runs a `.swb` |
| `RUNNER.SYSTEM` | Family B //e | 30,477 | 10,227 | runs a `.swb` (aux-paged) |
| `RUNNER.SYSTEM` | Family B Saturn | 33,134 | 7,570 | runs a `.swb` (Saturn-paged) |

The II+ REPL binaries (`SWIFTIIP` 12 B, `SWIFTSAT` MAIN **2 B**) sit
hard against the ceiling - this is why several //e-only features below
can't be carried on the II+, and why `SWIFTSAT`'s REPL cursor body had to
go to the XLC overlay (section 4).

---

## 2. Memory budgets (BSS / heap / arena)

| Region | Binary | Size | Notes |
|--------|--------|-----:|-------|
| REPL heap | lite / extras | shown by `:mem` | object heap; `:reset` clears it |
| Compiler bytecode arena | Family B | 1,834 B (Tier 1 flat) | //e-aux 896 / Saturn 640 B windows |
| Compiler const pool | Family B | 768 B (Tier 1) | 744 B //e-aux / 704 B Saturn |
| Runner `.swb` image buffer | Family B Tier 1 | 2,944 B | flat II+ Runner cap; Saturn / aux Runners stream through paged stores |
| Runner heap | Family B | II+ 2,136 / //e 2,560 / Saturn 1,792 B | trimmed per tier to fund `wait`/`tone`/`abs`/`sgn`, plus the II+ inverse-J/M render fix (see `RUNNER_DEFS`) |
| REPL line-history ring | //e only | ~1 KB BSS | 8 lines × 127 chars; II+ can't spare it |
| Launcher low-RAM workspace | per boot disk | 6 KB low RAM | editor gap/screen frame at `$0800-$1BFF` + fixed MLI buffer at `$1C00-$1FFF`; launcher SYS footprint is in the table above |

On flat Family B Tier 1, the **Runner's BSS = `.swb` image buffer + heap**
is the wall that sizes how big a compiled program can be. Saturn and aux
tiers move bytecode out of MAIN, but their runtime heaps are still fixed.

---

## 3. Core language (lite - ships on all four REPL binaries)

These are part of the base interpreter; their cost is *in* the lite
binary footprint above, not separately isolable.

| Feature | Cost | Notes |
|---------|------|-------|
| Int / String / Bool / arithmetic | base | tagged values |
| `let` / `var`, assignment, compound-assign | base | |
| String interpolation `\(…)`, concat, `.count`, indexing | base | |
| Optionals (`T?`, `nil`, `??`, force) | base | |
| `if` / `else if` / `else`, `if let` / `if let … else` | base | if-let-else funded by relocating array opcodes to XLC |
| `while`, `for-in` over ranges, `break` | base | direct array `for-in` is Family B |
| Functions, parameters, recursion, return | base | |
| Arrays (literal, index, `.count`, append) | base | walk by index in REPLs |
| Built-ins: `print`, `readLine`, `min`, `max`, `String(Int)` | base | |
| Apple II typing model (auto-lowercase, digraphs, case markers) | base | //e uses native case input |

---

## 4. REPL features

| Feature | Binaries | Disk cost | Memory cost |
|---------|----------|-----------|-------------|
| Meta-commands `:help` `:quit` `:reset` `:mem` `:list` | all four | base | - |
| Backspace line edit (`$08`) | all four | base | - |
| **Blinking cursor at the prompt** (40-col) | all four | ~180 B; `SWIFTIIP`/`SWIFTIIE`/`SWIFTAUX` in MAIN, **`SWIFTSAT` in the XLC overlay** (MAIN was full) | ~6 B BSS |
| **Line-history recall** (up/down, Ctrl-P/N) | **//e only** | compiler-side, in `SWIFTIIE`/`SWIFTAUX` 1 KB+ headroom | **~1 KB BSS ring** |
| **Function redefinition** (`func` rebind + `redef` notice) | **//e only** | **~340 B compiler-side** | dead arena space until `:reset` |
| Cold-reboot-to-menu on `:quit` | all four | base | - |

The two //e-only REPL features are absent on `SWIFTIIP` / `SWIFTSAT`
purely on budget (2–12 B headroom can't absorb 340 B + 1 KB BSS).

**Blinking cursor.** The 40-col prompt hand-rolls an inverse-block cursor (as
the editor and file-selector do), blinking at the shared `0x1FFF` cadence;
80-column defers to the card firmware's own cursor. On `SWIFTSAT`, whose MAIN is
full, the cursor body sits in the Saturn bank-1 XLC overlay and the REPL routes
its line read through the XLC trampoline to reach it - see
[design 011](../contributing/design/011-extras-lc-in-saturn-aux.md).

---

## 5. Extras / platform built-ins (Saturn + //e aux only)

Hot bodies live in MAIN; cold bodies are paged to the **Saturn XLC bank**
(II+) or **aux park** (//e). Per-builtin cost ≈ a parser-table row in MAIN
+ a dispatcher body in the overlay.

| Feature group | Built-ins | Residency / budget note |
|---------------|-----------|-------------------------|
| String conversions | `asc`, `chr`, `Int(s)` | XLC body |
| Array methods | extras `.`-methods | NEW_ARRAY / ARR_LEN relocated to XLC |
| Text control | `home`, `htab`, `vtab` | XLC; `home` ~163 B MAIN when tried on lite |
| GR graphics | `gr`, `grFull`, `text`, `color`, `plot`, `hlin`, `vlin`, `scrn` | grouped park overlay (GR group ~2.6 KB) |
| Memory access | `peek`, `poke` (+ free speaker click) | small MAIN |
| Timing | `wait(_ ms:)` | **Family B only** - Compiler/Runner (II+ and //e) + host. A delay is a program builtin, not a REPL one; each Runner = +86 B BSS paid by a 128 B heap trim. No REPL ships it |
| Sound | `tone(_ halfPeriod:_ cycles:)` | **Family B only** - square-wave speaker tone, same residency as `wait()`. Each Runner = ~200 B BSS paid by a 256 B heap trim; the //e Compiler also took a 32 B stack-reserve trim. No REPL ships it (SWIFTSAT's MAIN, now 2 B, can't even fit the recognizer row, so SWIFTAUX was kept symmetric) |
| Core builtin bodies | `print`/`readLine`/`min`/`max` relocated | +749 B SWIFTSAT MAIN reclaimed via XLC |
| **80-column** `text80()` | //e firmware + II+ Videx | **//e: `SWIFTIIE` +315 B, `SWIFTAUX` +152 B**; II+ REPL path is `SWIFTSAT`, so it needs Saturn + Videx (~+183 B); flat II+ Family B Runner can drive Videx for program output; off = byte-identical |

`SWIFTSAT` MAIN runs at **2 B** headroom - the platform parser table is
MAIN-resident (only helper bodies relocate), which is the wall the SWIFTAUX
"packed directory" copy-down design was built around, and which forced the
REPL cursor body into the XLC overlay (section 4).

---

## 6. Editor (merged into the launcher)

| Feature | Cost | Notes |
|---------|------|-------|
| Gap-buffer multi-line editing | in launcher (5+ KB headroom) | "FILE TOO BIG TO EDIT" past gap-buffer cap |
| Movement (arrows, Ctrl-O/L/K/J/A/E/T/V), delete, insert | in launcher | |
| //e cursor-move + typing + status fast paths | ~1,108 B //e | needs wrap-aware tables |
| II+ typing + status-on-pause fast paths | funded by dropping II+ disk-space readout | cursor-move wrapped-row case stays //e-only |
| Save indicator, restore cursor on reopen | small | |

---

## 7. Launcher + diagnostics

| Feature | Cost | Notes |
|---------|------|-------|
| Boot menu (REPL / file selector / Debug / About) | in launcher | |
| File selector (list/preview/cd/run/CRUD/drive-pick) | in launcher | text preview pane; //e 80-col full-width |
| `DEBUG.SYSTEM` 3-page hardware diagnostic | **3,784 B** standalone | loads `$2000`, no BSS ceiling; II+ gets disk-space readout here |
| //e launcher disk-space readout | `LITE_IIE`-gated, //e launcher only | II+ launcher ~460 B over its ceiling for it |

---

## 8. Family B - Compiler + Runner (bigger programs)

Separate disks. Compile `.swift` → `.swb`, then run the `.swb` on any
machine. Source is streamed from disk on every tier; the three tiers differ in
where compiled **bytecode** lives and therefore how large a function-heavy
program can get.

| Feature | Cost | Notes |
|---------|------|-------|
| Compiler (`.swift` → `.swb`) | II+ 35,032 / //e aux 35,704 / Saturn 36,119 B | flat arena 1,834; aux/Saturn page completed functions |
| Runner (runs `.swb`) | II+ 31,940 / //e 30,477 / Saturn 33,134 B | image buf 2,944 on flat II+ / heap 2,136, 2,560, 1,792 |
| **Aux-paged bytecode** (//e Tier-3) | code ceiling ~2.9 KB → ~40 KB | 1 KB MAIN window + ROM AUXMOVE |
| **Saturn-paged bytecode** (II+ Tier-2) | similar | Saturn bank window |
| `for-in` / `switch` (Int/Bool) / `random(in:)` | funded by dropping `array.c` (~1.5 KB) + arena slack | xorshift `random` is multiply-free |
| File / directory CRUD builtins | unified host+Runner path | read/write/append/delete/rename/exists/createDir/deleteDir/listDirectory; `WITH_FILE_CRUD` keeps them out of the Compiler |
| `abs(_:)` / `sgn(_:)` (Int → Int) | each Runner −640 B heap (with the string methods); Compiler −224 B C-stack | **Family B only** - pure Int math; the II+ lite REPL (no flags, no XLC) overflowed by ~280 B, so they don't reach the REPLs |
| `s.hasPrefix(_:)` / `s.hasSuffix(_:)` (String → Bool) | folded into the array-method recognizer (~40 B vs ~300 B) | **Family B only** - reverses the twice-dropped scope call; shares the Runner heap trim above |
| Ctrl-C break (Runner) | small | breaks a program parked in `readLine` |
