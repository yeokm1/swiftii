# TESTING-emulators.md - SwiftII emulator acceptance pass

This is the **emulator half** of the v1.0 acceptance playbook: a
self-contained checklist that exercises *every* shipped SwiftII feature on
**every** distribution disk using only **izapple2** and **Mariani**. Tick
each box off as it passes.

Use it for three things:

1. **Pre-flight before iron.** Run this clean first; only then touch
   hardware (the physical II+ pass lives in
   [`TESTING-iiplus.md`](TESTING-iiplus.md)).
2. **Cover the machines you don't have on the bench.** The **//e** disks
   (//e lite, //e aux, the //e Tier-3 compiler) and every **[//e]-only**
   behaviour can *only* be checked here unless you own a //e.
3. **A complete pass over every disk - including the II+ ones.** Run the
   **[II+]** / **[SAT]** rows here too, even though the physical II+ rig
   ([`TESTING-iiplus.md`](TESTING-iiplus.md)) re-covers them on iron. The
   emulator pass is meant to be self-complete: it exercises *every* shipped
   feature on *every* distribution disk, so the II+ disks are **not**
   skipped just because you can also boot them on hardware. The iron pass
   is the final acceptance gate, not a substitute for testing II+ here.

It is distinct from [`TESTING.md`](TESTING.md), the contributor-facing
reference for the automated host / sim / integration / REPL suites.

> **Automated alternative.** The self-checking subset of this playbook now runs
> hands-free via the izapple2-driven harness in
> [`tools/host/acceptance/`](../../tools/host/acceptance) - `make acceptance` boots every
> config, injects keystrokes, runs the launcher's **RUN TESTS** sweep, snapshots
> the graphics / 80-col screens, and reports pass/fail across the matrix
> (Family B verdicts come from the on-disk `TESTLOG`). It drives izapple2's
> `headless` frontend (embedded ROMs - nothing to source; see
> [`BUILDING.md`](../contributing/BUILDING.md), the acceptance-harness section). This manual
> playbook stays the reference for the visual / audio / feel checks the harness
> can't make.

> For a key-by-key sweep of **every keyboard shortcut in every UI mode**
> (launcher, file selector, editor, REPL, Debug, test runner, 80-col),
> use the companion [`TESTING-keyboard.md`](TESTING-keyboard.md) matrix.
> This playbook walks *features*; that one walks *keys*.

---

## Legend

Each step carries two kinds of tag: **which machine** it needs, and
**which class of disk** it runs from. A step tagged **[REPL][//e]** means
"a REPL interpreter disk, on a //e"; **[CR][II+]** means "a Compiler +
Runner (Family B) disk, on a II+".

**Disk class:**

| Tag | Meaning |
|-----|---------|
| **[REPL]** | A **REPL interpreter** disk - interactive prompt (`SWIFTIIP` / `SWIFTSAT` / `SWIFTIIE` / `SWIFTAUX`) |
| **[CR]** | A **Compiler + Runner** disk - Family B, edit → compile (`.swb`) → run; no REPL |

**Machine / hardware (the emulator config the `make run-*` target boots):**

| Tag | Meaning |
|-----|---------|
| **[II+]** | A II+ / ][ config (lite path; no extra cards) |
| **[SAT]** | A config with a **Saturn 128K** (or RamWorks-class) card for the extras binary |
| **[//e]** | An Apple //e config (AUX path needs the 80-col / 64K aux card) |
| **[VIDEX]** | A **Videx Videoterm** config for 80-column - **izapple2 only**; the REPL smoke target is II+ Saturn + Videx |
| **[DRIVE2]** | Needs the data disk in drive 2 - append `-2disk` to the run target |

> **[DRIVE2] steps** (the on-disk `TESTS/` / `XTESTS/` / `FBTESTS/` suites
> and the file selector's `,` drive-pick) need the data disk mounted in
> drive 2: use the **`-2disk`** variant of the run target
> (`run-iz-*-2disk` / `run-mari-*-2disk`). Without it, every program disk
> still ships the on-disk **`SAMPLES/`**, so you have programs to run.

The eight distribution disks (built by `make disks`). Both emulators cover
REPL and compiler-runner disks; the data-disk variants share the `-2disk`
suffix on both sides:

| Disk | Class | `make` target | Binary | izapple2 | Mariani |
|------|-------|---------------|--------|----------|---------|
| II+ lite | REPL | `disk-iip-lite-repl` | `SWIFTIIP` | `run-iz-iip` | `run` / `run-mari-iip` |
| II+ Saturn | REPL | `disk-iip-sat-repl` | `SWIFTSAT` | `run-iz-sat` | `run-sat` / `run-mari-sat` |
| //e lite | REPL | `disk-iie-lite-repl` | `SWIFTIIE` | `run-iz-iie` | `run-iie` / `run-mari-iie` |
| //e aux | REPL | `disk-iie-aux-repl` | `SWIFTAUX` | `run-iz-iienh` | `run-aux` / `run-mari-aux` |
| Tier-1 compiler | CR | `disk-iip-compiler` | Compiler+Runner | `run-iz-compiler` | `run-mari-compiler` |
| Tier-2 Saturn compiler | CR | `disk-iip-sat-compiler` | Compiler+Runner | `run-iz-compiler-sat` | `run-mari-compiler-sat` |
| Tier-3 //e compiler | CR | `disk-iie-compiler` | Compiler+Runner | `run-iz-compiler-iie` | `run-mari-compiler-iie` |
| Data | - | `disk-data` | - | drive 2 of `run-iz-*-2disk` | drive 2 of `run-mari-*-2disk` |

**Known emulator-only quirks** (don't chase them as bugs):

- **izapple2** `text()` does **not** revert from 80→40 col (real HW does).
- **Videoterm ROM divergence** - izapple2's Videoterm `$FFCB` differs from
  a real card; minor cosmetic glitches in `run-iz-videx` are expected. This
  boots `SWIFTSAT`, so the profile is II+ Saturn + Videx, not Videx-only.
- Mariani / AppleWin have **no Videoterm**; the Videx 80-col path can only
  be exercised here on `run-iz-videx` (II+ Saturn + Videx) or on real HW.
- **Sound is silent** on emulators that don't model the speaker - the
  `ttone` audio check (Section 7) is a real-HW-only step; see
  [`TESTING-iiplus.md`](TESTING-iiplus.md).

---

## Execution run-sheet (do this in order)

The feature sections below are the **canonical per-feature reference** -
read them for the *what* and the expected output. **This run-sheet is the
*order* to execute them in.**

Emulators have **no disk-swap cost** (every `make run-*` builds and
launches its own disk), so the only thing worth minimizing is **switching
emulator apps**. Do it **one emulator at a time**, not one disk at a time:
run the complete **izapple2** pass first, then a quick **Mariani**
regression pass over the subset it can run. Append `-2disk` to pull in the
data disk for the **[DRIVE2]** checks.

**Per-boot ordering rule:** inside one boot, do the launcher first, then
stay in each mode while you're in it - REPL work together, then back to the
launcher for the file selector → editor → Debug → samples - so you change
UI mode the fewest times.

### Pass 1 - izapple2 (the full pass; superset of Mariani)

izapple2 is the only emulator that can do Videx 80-col (E3), the original
`][` boot, and `bigswb-iie` - so this pass covers **everything**.

| # | Boot (izapple2 / +data) | Sections to clear in this boot |
|---|--------------------------|--------------------------------|
| E1 | `run-iz-iip` / `-iip-2disk` | **0** (W is a no-op on plain II+), **1** (full core session + meta-cmds + line editing - confirm history **ABSENT** & redef **ERRORS**), **4** (editor, II+ paths), **3** (file selector; `,` drive-pick needs `-2disk`), **8** (lite samples), **5** (Debug); with `-2disk`: **7 `TESTS/`** |
| E2 | `run-iz-sat` / `-sat-2disk` | Everything in E1 **plus 2** (extras builtins, graphics, memory, speaker clicks), **8** extras (`x*`) samples; with `-2disk`: **7 `XTESTS/`** |
| E3 | `run-iz-videx` / `-videx-2disk` (II+ Saturn + Videx) | 80-col rows only: **0** `W` toggle, **2** `text80()`/`htab(70)` (ROM divergence expected) |
| E4 | `run-iz-iie` / `-iie-2disk` | E1's sections with **//e paths**, **plus** the **[//e]-only** rows: **1** history recall (Ctrl-P/N) + function redefinition; **5** //e disk-space readout |
| E5 | `run-iz-iienh` / `-iienh-2disk` | Everything in E4 **plus 2** (extras), **8** extras samples; with `-2disk`: **7 `XTESTS/`** |
| E6 | `run-iz-compiler` / `-compiler-2disk` | **0**, **3**, **4** (Ctrl-R = save+compile+run), **5**, **6** Tier-1, **8** Family-B samples; with `-2disk`: **7 `FBTESTS/`** |
| E7 | `run-iz-compiler-sat` | **6** Tier-2 (Saturn-paged bytecode) |
| E8 | `run-iz-compiler-iie` + `run-iz-bigswb-iie` | **6** Tier-3 (aux-paged bytecode), big-program driver |

### Pass 2 - Mariani (regression re-confirm; no Videoterm)

Same section lists as the matching E-row above - you already know the
expected output, so this is a fast confirmation that nothing is
Mariani-specific. **Skip E3 (no Videoterm)** and the `bigswb-iie` half of
E8.

| repeats | Mariani command (`run` / `run-mari-*`) |
|---------|----------------------------------------|
| E1 | `run` (or `run-mari-iip`) / `run-mari-iip-2disk` |
| E2 | `run-sat` (or `run-mari-sat`) / `run-mari-sat-2disk` |
| E4 | `run-iie` (or `run-mari-iie`) / `run-mari-iie-2disk` |
| E5 | `run-aux` (or `run-mari-aux`) / `run-mari-aux-2disk` |
| E6 | `run-mari-compiler` / `run-mari-compiler-2disk` |
| E7 | `run-mari-compiler-sat` |
| E8 | `run-mari-compiler-iie` (Tier-3 only; no big-program target) |

---

## Section 0 - Boot and launcher

For each of the four REPL program disks (II+ lite, II+ Saturn, //e lite,
//e aux) and, separately, the three Family B compiler disks:

- [ ] **Boots to the launcher menu** without a monitor trap or garbage
      screen. **[II+]** / **[//e]**
- [ ] The menu shows the four options: **1 REPL** (compiler disks: **1
      File Selector**), **2 File Selector**, **3 Debug**, **4 About**.
- [ ] **Menu navigation:** `I` / `M` move the `>` highlight; **Return**, the
      **→ right-arrow**, or a number key activates it (prompt:
      `I/M move  Ret/-> or 1-5 select >`).
- [ ] **About (4)** shows the correct version line and the disk-set blurb,
      and the banner is **machine-tagged** correctly:
      - II+ lite → `SwiftII ][+`
      - II+ Saturn → `SwiftII ][+ Saturn`
      - //e lite → `SwiftII //e`
      - //e aux → `SwiftII //e aux`
- [ ] **Launcher text rendering:** on **[II+]** lowercase shows as NORMAL
      uppercase and ASCII-uppercase shows as INVERSE; on **[//e]** native
      mixed case. No stray inverse/flashing characters.
- [ ] **40/80 toggle `W`** flips the launcher between 40- and 80-column.
      **[//e]** firmware 80-col only. On II+ - including `run-iz-videx` -
      the launcher stays 40-column and `W` is a no-op.

---

## Section 1 - REPL: core language (lite path)

Run on the **II+ lite** and **//e lite** disks → menu option **1 REPL**.
Everything here must also work on the extras disks. **[REPL]**,
**[II+]** / **[//e]**.

> **How to use the scripts in this section.** Type each line shown after
> the `> ` prompt **verbatim** and press Return; the line(s) directly
> beneath it are the **exact expected output**. A blank line under an
> input means that input is **silent** (`let` / `var` / assignment / a
> `func` definition print nothing). On a **][+** keyboard, get capitals
> with the apostrophe markers - type `'WOZ` for `Woz`, `'INT` for `Int`,
> `''` before a run - the interpreter stores the lowercase-canonical form
> shown here. The core language is *also* covered by the on-disk `TESTS/`
> suite and the `fizzbuzz` / `functions` / `arrays` / `optionals` /
> `strings` samples (run them too); this typed session is the interactive
> hands-on confirmation of the same surface.

- [ ] Banner + `Type :help :list :quit` prints; prompt is `> `.
- [ ] **Type this session at the prompt; every line of output must match
      exactly:**

```
> 1 + 2
3
> 7 / 2
3
> 7 % 2
1
> 3 * -4
-12
> let name = "Woz"
> var n = 1
> n = n + 1
> n
2
> print("Hello, \(name)!")
Hello, Woz!
> let s = "ab" + "cd"
> print(s)
abcd
> s.count
4
> 2 < 3
true
> 2 == 3
false
> if n % 2 == 0 { print("even") } else { print("odd") }
even
> var i = 0; while i < 3 { i = i + 1; print(i) }
1
2
3
> for k in 1...3 { print(k * k) }
1
4
9
> for v in [10, 20, 30] { print(v) }
10
20
30
> var c = 0; while true { c = c + 1; if c == 2 { break }; print(c) }
1
> func add(a: Int, b: Int) -> Int { return a + b }
> add(a: 2, b: 3)
5
> let maybe: Int? = nil
> maybe ?? 99
99
> if let g = Int("42") { print(g * 2) } else { print("not a number") }
84
> if let bad = Int("xx") { print(bad) } else { print("not a number") }
not a number
> var xs = [10, 20, 30]
> xs.append(40)
> xs.count
4
> xs[1]
20
> min(3, 8)
3
> max(-4, 5)
5
```

- [ ] **`readLine` (interactive):** type `let who = readLine() ?? "?"` and
      Return, then type `Alice` and Return, then `print(who)` → `Alice`.
- [ ] **`let` is immutable:** typing `name = "x"` prints a clean error
      (cannot assign to a `let` constant) and the session continues with
      bindings intact.
- [ ] **Recursion** is covered by the bundled **`functions`** sample (run
      it) - it exercises `square` / `power` / bounded recursion / `min` /
      `max`. Recursion is capped at **4 call frames**, so a deeper
      recursion errors cleanly rather than crashing.

### REPL meta-commands

- [ ] `:help` lists the commands.
- [ ] `:mem` prints used / free heap bytes.
- [ ] `:list` lists current bindings as `let/var name = value` in
      definition order.
- [ ] `:reset` clears all bindings + functions and resets the heap
      (verify with `:list` after).
- [ ] `:quit` exits to the launcher; **Ctrl-D** on an empty line does the
      same (EOF).
- [ ] **`:quit` returns to the boot menu** (cold-reboot-to-menu), not a
      hang.

### REPL line editing

- [ ] **Backspace / left-arrow (`$08`)** deletes the char to its left -
      **all four binaries**.
- [ ] **Line-history recall - [//e] only.** Up-arrow / **Ctrl-P** walks
      back through the last 8 input lines; down-arrow / **Ctrl-N** walks
      forward; down past newest restores the in-progress line. A recalled
      line can be edited and re-run.
- [ ] **History is correctly ABSENT on [II+]** (SWIFTIIP / SWIFTSAT): the
      up/down keys do nothing special; only backspace edits.

### Function redefinition

- [ ] **[//e] only:** redefining `func foo()` rebinds it and prints a
      `redef foo` notice; later calls use the new body; a failed
      recompile keeps the old body (atomic).
- [ ] **[II+] correctly errors** on redefinition (use `:reset` to redefine).

---

## Section 2 - REPL: extras (Saturn / //e aux)

Run on the **II+ Saturn** (`SWIFTSAT`) and **//e aux** (`SWIFTAUX`) disks.
These are the lite surface **plus** the platform built-ins. **[REPL]**,
**[SAT]** / **[//e aux]**.

All of Section 1 must pass here too; the steps below are the **extras
surface on top**. The typed-script convention is the same (type after
`> `, output shown beneath; blank = silent).

- [ ] Banner reads `SwiftII ][+ Saturn` / `SwiftII //e aux`.
- [ ] **String / int builtins - type this; output must match:**

```
> print(asc("A"))
65
> print(chr(66))
B
> print(String(42))
42
```

- [ ] **Array extras methods (`.contains` / `.removeLast`) - type this:**

```
> var a = [1, 2, 3]
> a.contains(2)
true
> a.removeLast()
3
> a.count
2
```

- [ ] **Text-mode cursor control:** type `home()` (screen clears, cursor
      to top-left), then `vtab(10)`, then `htab(5)`, then `print("X")` -
      the `X` appears at **row 10, column 5**.
- [ ] **GR low-res graphics - type this; a drawing appears:**

```
> gr()
> color(15)
> hlin(0, 39, 0)
> vlin(0, 39, 0)
> color(1)
> plot(20, 20)
> scrn(20, 20)
1
> text()
```
      A white top edge + left edge appear with a magenta dot near the
      centre; `scrn(20, 20)` reads that dot's colour back (`1`); `text()`
      returns to the text screen (**izapple2 does not revert - known
      quirk; real HW does**).
- [ ] **Sound + memory:** typing `poke(49200, 0)` should make a speaker
      click (silent on emulators that don't model the speaker - confirm on
      real HW per [`TESTING-iiplus.md`](TESTING-iiplus.md)); `peek(49152)`
      returns the keyboard byte (≥ 128 when a key is waiting).
- [ ] **80-column:** type `text80()` (switches to 80 columns), then
      `htab(70)` and `print("R")` - the `R` lands at **column 70**; `text()`
      reverts to 40. **[//e]** firmware path / **[VIDEX]** `run-iz-videx`
      path (II+ Saturn + Videx).

---

## Section 3 - File selector (file-manager mini-UI)

Menu option **2 File Selector** (compiler disks: **1**). Test against a
real ProDOS directory; a **data disk in drive 2** (the `-2disk` targets)
adds `TESTS/` + extra `SAMPLES/`. **[REPL]** + **[CR]**, **[II+]** / **[//e]**.

- [ ] **List + highlight:** files show with the per-file gutter; up/down
      (J/K) move the highlight and repaint only the preview.
- [ ] **Text preview pane** shows the highlighted `.swift` contents
      (80-col full-width on **[//e]**).
- [ ] **`,` re-picks the drive** - switch to drive 2 (data disk) and back.
      **[DRIVE2]** (use a `-2disk` target).
- [ ] **`RET` opens** a file in the editor (or launches a `.SYSTEM` file).
- [ ] **`X` runs** a `.SWIFT` program (REPL disks) - output renders, then
      a readable return to the launcher.
- [ ] **`E` edits**, **`F` new file**, **`R` rename**, **`D` delete**,
      **`N` new folder** - each acts on the real ProDOS directory; verify
      the change persists (re-list).
- [ ] **`cd`** into a subdirectory and back works.

---

## Section 4 - Editor

Open a file via the file selector (`RET` / `E`). The editor is merged into
the launcher (in-process), so it's on **both disk classes** - **[REPL]** +
**[CR]** (on a CR disk, Ctrl-R = save + compile + run). **[II+]** / **[//e]**.

- [ ] **Typing** inserts printable ASCII; case renders correctly
      (**[II+]** auto-lowercase to canonical buffer + inverse/normal video;
      **[//e]** native mixed case).
- [ ] **Ctrl-D** backspaces (deletes the char to the left); **//e Delete
      (`$7F`)** deletes left too.
- [ ] **RETURN** inserts a newline; multi-line editing works (this is the
      multi-line authoring path the REPL lacks).
- [ ] **Cursor movement:** the **← / → arrows** move left/right
      non-destructively (Apple-Pascal style); **Ctrl-O / Ctrl-L** move up/down
      a line (the Apple Pascal pair — also Ctrl-K / Ctrl-J, or the //e up/down
      arrows); **Ctrl-A/E** (line start/end), **Ctrl-T/V** (page up/down). The
      cursor stays visible while moving (movement is fully non-modal — no ESC
      cursor mode). On II+ cooked Swift files, **Ctrl-W** inserts `_` and
      **Ctrl-G** toggles cooked ↔ raw mode.
- [ ] **Wrapped-line navigation** behaves correctly (**[//e]** wrap-aware
      cursor-move; **[II+]** single-row-line locate).
- [ ] **Save indicator** in the status row reflects dirty/saved state.
- [ ] **Ctrl-S saves**; **Ctrl-R runs** (REPL disks) / **save+compile+run**
      (compiler disks); **Ctrl-Q** returns to the browser (open another file
      from there — there is no in-editor open).
- [ ] **End-to-end edit → run.** Press **F** for a new file, type this
      program exactly (all lowercase - no capitals or `\` needed, so it
      types cleanly on a ][+), then press **Ctrl-R**:

```
var total = 0
for i in 1...10 {
  total = total + i
}
print("sum of 1 to 10:")
print(total)
```
      Expected output, then a keypress returns to the editor:

```
sum of 1 to 10:
55
```
- [ ] **Ctrl-R then reopen** restores the saved cursor position.
- [ ] **"FILE TOO BIG TO EDIT"** appears for a file larger than the gap
      buffer capacity (don't truncate it).
- [ ] **Responsiveness:** typing and cursor-move feel immediate
      (**[//e]** fast paths; **[II+]** typing + status-on-pause fast paths).

---

## Section 5 - Debug diagnostic (`DEBUG.SYSTEM`)

Menu option **3 Debug** (compiler disks: **2**). Arrows page, **ESC**
exits. Three pages. **[REPL]** + **[CR]**, **[II+]** / **[//e]**.

- [ ] **Page 1 - VOLUMES:** free / total shown in **both KB and blocks**
      for each online volume. **[SAT]** stands in for the Language Card.
- [ ] **Page 2 - DETECTION:** machine type (a real **[II+]** must show
      **II+**, not //e — `$FBB3` reads `$EA`, not `$38`), explicit
      **Saturn slot**, the **AUX RAM** readout (moved here from the old
      MEMORY page; **[//e]** shows the aux-card 3-state, II+ shows N/A),
      CPU, ProDOS / boot slot+drive, and prefix.
- [ ] **Page 3 - SLOTS.**
- [ ] All pages are **uppercase-only** on **[II+]** (no lowercase via COUT);
      readable on **[//e]**.
- [ ] **ESC returns** to the launcher cleanly.
- [ ] **//e launcher disk-space readout** (the `LITE_IIE`-gated free/total-KB
      line on the //e launcher's volume picker) shows. **[//e]** only.

---

## Section 6 - Family B: Compiler + Runner

The three compiler disks. Flow: **edit → compile (.swift → .swb) → run**.
**[CR]**. No Saturn/aux card needed to *run* a `.swb`; tiers differ in how
much **source** the Compiler can handle. **No typing required for the
end-to-end checks** - each compiler disk ships `SAMPLES/` (e.g.
`GREET.SWIFT`, `FUNCTIONS.SWIFT`, `XSNAKE.SWIFT`, and the Family-B feature
tour `XDICE.SWIFT`); press **`X`** on one to compile + run it. (To exercise
the editor → compile path with your own code, type the Section 4 program
and press **Ctrl-R**, which on a compiler disk = save + compile + run.)

- [ ] **Tier-1 (II+, `run-iz-compiler`):** `X` on a sample `.swift`
      (e.g. `GREET.SWIFT`) compiles to `.swb` and runs end-to-end; `X` on
      the resulting `.swb` runs it directly without recompiling. **[II+]**
- [ ] **Tier-2 (II+ Saturn, `run-iz-compiler-sat`):** compile + run a
      **bigger** program - confirm the **Saturn-paged bytecode** driver
      works. **[SAT]**
- [ ] **Tier-3 (//e aux, `run-iz-compiler-iie`):** compile + run a big
      program - confirm the **aux-paged bytecode** driver works
      (the big-program path is `run-iz-bigswb-iie`). **[//e aux]**
- [ ] **Ctrl-C breaks** a Runner program parked in `readLine` and returns
      to a readable launcher.
- [ ] **File I/O builtins** (Runner only): `readFile` / `writeFile` /
      append / delete / rename / exists / createDir / deleteDir /
      `listDirectory` round-trip against a real ProDOS data disk (drive 2).
      The exhaustive `FBTESTS/` suite covers these - run them. **[DRIVE2]**
      (use a `-2disk` target).
- [ ] **`for-in` / `switch` / `random`** in a compiled program produce the
      expected output, and **`random` actually varies** run-to-run.
- [ ] **Graphics from a compiled program** draws, then returns to a
      readable launcher.

---

## Section 7 - On-disk automated suites (data disk)

Mount the **data disk** (drive 2 - the `-2disk` targets). These are the
scripted suites, run from the file selector / Runner. **[REPL]** + **[CR]**,
**[II+]** / **[//e]**, **[DRIVE2]** throughout.

- [ ] **`TESTS/`** (general language) - run, expect 0 failures.
- [ ] **`XTESTS/`** (extras) - run on an extras / aux disk, expect 0
      failures.
- [ ] **`FBTESTS/`** (Family B) - run via a compiler disk + data disk on
      drive 2, expect **fail 0** each: `tfileio` (17, file CRUD vs real
      MLI), `tfiledir` (22, directory CRUD), `tswitch` (12), `tforarr` (7),
      `trandom` (9), `twait` (3), `ttone` (3), `tmath` (18).
- [ ] **`twait` shows the delay** - after `wait 1 s ...` / `wait 2 s ...` /
      `wait 3 s ...` it must pause **1, then 2, then 3 seconds** (rising,
      visibly different gaps). Instant on host; timing is approximate under
      emulation - eyeball it as roughly rising, and confirm exactly on real
      HW per [`TESTING-iiplus.md`](TESTING-iiplus.md). `wait()` is
      Family-B-only - the REPLs reject it ("undeclared name"), expected.
- [ ] **`ttone` is real-HW-only audio.** It must HEAR five rising blips
      then a low→high chirp; that needs the modelled speaker, so it's
      **deferred to real HW** ([`TESTING-iiplus.md`](TESTING-iiplus.md)).
      Here, just confirm the suite **runs to `fail 0`** (the test asserts
      the toggles happened; you simply can't hear them). `tone()` is
      Family-B-only - the REPLs reject it ("undeclared name"), expected.
- [ ] **`tmath` math + string methods** - `abs`/`sgn`/`hasPrefix`/`hasSuffix`,
      expect **`pass 18 fail 0`** (pure computation, so host and target agree -
      no audio/timing to eyeball). All four are Family-B-only: a Family A REPL
      rejects `abs(-5)`/`sgn(-5)` as "undeclared name" and
      `"x".hasPrefix("y")` as "unknown member", expected.

---

## Section 8 - Samples

The `SAMPLES/` set ships on every program disk; the data disk carries the
full set. **[II+]** / **[//e]**.

- [ ] Each lite sample runs from the file selector: `fib`, `fizzbuzz`,
      `greet`, `strings`, `arrays`, `functions`, `optionals`.
- [ ] Each extras-REPL (`x`-prefixed) sample runs on an extras / aux disk:
      `xgraphics`, `xsnake`, `xspeaker`, `xvtab`.
- [ ] Each Family-B-only (`x`-prefixed) sample runs with `[X]` on a compiler
      disk + data disk (and is rejected by every REPL): `xdice`, `xwide`.
- [ ] At least one **GR graphics** demo (`xgraphics` / `xsnake`) renders
      visibly and returns cleanly.

---

## Sign-off (emulators)

- [ ] **izapple2 Pass 1** - E1–E8 green (every section above).
- [ ] **Mariani Pass 2** - the regression subset green.
- [ ] **Keyboard matrix** - a full pass of
      [`TESTING-keyboard.md`](TESTING-keyboard.md) (every ✋ and ◐ row ticked).
- [ ] No regressions in the automated suites: `make ci` green.

When this is clean, you've pre-flighted v1.0 in software - proceed to the
real-hardware gate in [`TESTING-iiplus.md`](TESTING-iiplus.md).
