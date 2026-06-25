# TESTING-iiplus.md - SwiftII physical II+ acceptance gate

This is the **real-hardware half** of the v1.0 acceptance playbook,
specialised to the bench rig: a **II+ with a Saturn 128K** card (which also
stands in for a plain 16 KB Language Card when extras aren't needed), a
**Videx Videoterm** 80-column card (**removable on demand** to check the
40-column fallback), and **two drives** (drive 2 carries the data disk).
Passing every box here - *"this is the goal!"* - is the v1.0 acceptance
gate.

**Do the emulator pass first.** Run
[`TESTING-emulators.md`](TESTING-emulators.md) clean before touching iron.
It catches the easy failures cheaply and is the *only* place the **//e**
disks (//e lite, //e aux, the //e Tier-3 compiler) and **[//e]-only**
behaviours get exercised. **None of those run on this II+ rig** and they
are intentionally absent below.

It is distinct from [`TESTING.md`](TESTING.md), the contributor-facing
reference for the automated host / sim / integration / REPL suites.

> For a key-by-key sweep of **every keyboard shortcut in every UI mode**
> (launcher, file selector, editor, REPL, Debug, test runner, 80-col),
> use the companion [`TESTING-keyboard.md`](TESTING-keyboard.md) matrix.
> This playbook walks *features*; that one walks *keys*.

> **How to use this on hardware.** Write each `.po` to a real 5.25"
> floppy (ADTPro, a Floppy Emu, or equivalent), boot it, and follow the
> [**run order**](#priority-run-order-do-this-in-order) below. It puts the
> hardware-sensitive, manual-only checks first, then groups the remaining
> work by boot disk and card state. Keep the **data disk in drive 2**
> throughout.

---

## Legend

Each step carries a **disk class** tag and, where relevant, a **card /
drive** tag. A step tagged **[REPL]** runs on a REPL interpreter disk;
**[CR]** on a Compiler + Runner (Family B) disk.

**Disk class:**

| Tag | Meaning |
|-----|---------|
| **[REPL]** | A **REPL interpreter** disk - interactive prompt (`SWIFTIIP` / `SWIFTSAT`) |
| **[CR]** | A **Compiler + Runner** disk - Family B, edit → compile (`.swb`) → run; no REPL |

**Card / drive:**

| Tag | Meaning |
|-----|---------|
| **[SAT]** | Needs the **Saturn 128K** card (the extras binary; also stands in for a 16 KB Language Card) |
| **[VIDEX]** | Needs the **Videx Videoterm** for 80-column |
| **[DRIVE2]** | Needs the data disk in **drive 2** |
| **PULL → fallback** | Remove the named optional card only when the remaining machine still meets ProDOS's 64K / LC requirement |

The five disks this rig boots (built by `make disks`):

| Disk | Class | `make` target | Binary | Needs |
|------|-------|---------------|--------|-------|
| II+ lite | REPL | `disk-iip-lite-repl` | `SWIFTIIP` | II+ + 16 KB LC (Saturn OK) |
| II+ Saturn | REPL | `disk-iip-sat-repl` | `SWIFTSAT` | **[SAT]** |
| Tier-1 compiler | CR | `disk-iip-compiler` | Compiler+Runner | II+ + 16 KB LC (Saturn OK) |
| Tier-2 Saturn compiler | CR | `disk-iip-sat-compiler` | Compiler+Runner | **[SAT]** |
| Data | - | `disk-data` | - | **[DRIVE2]** |

> The other three distribution disks (//e lite, //e aux, //e Tier-3
> compiler) need an Apple //e - they are out of scope for this rig; cover
> them in [`TESTING-emulators.md`](TESTING-emulators.md).

If you ever run a program disk **single-drive**, every program disk also
ships the on-disk **`SAMPLES/`**, so you still have programs to run without
the data disk - but the **[DRIVE2]** suites need drive 2.

---

## Priority run order (do this in order)

The feature sections below are the **canonical per-feature reference**:
read them for the *what* and the expected output. The stage list here is
ordered by **risk when bench time is limited**: manual, hardware-sensitive
checks first; checks with strong automated-runner coverage later. A floppy
swap, reboot, or card pull is still expensive, so each stage groups nearby
checks for the current disk and card state.

The 30-minute stability requirement in **Section 9** is not a separate
repeat pass. Count the time spent during the relevant REPL stages:
approximately 15 minutes on `SWIFTSAT` in stages 1 / 2 and 15 minutes on
`SWIFTIIP` in stage 5. If either path runs short, stay in that REPL and
repeat samples / typed snippets until the time is met.

If you have to stop early, stop after the highest completed stage and record
which lower-priority stages remain. Do **not** skip the emulator pre-flight:
this priority order assumes [`TESTING-emulators.md`](TESTING-emulators.md)
and the automated acceptance harness were already clean.

### Stage 1 - Saturn REPL, manual hardware surface

- **Cards:** Saturn + Videx **IN**
- **Boot:** II+ Saturn REPL (`SWIFTSAT`)
- **Why first:** most of this is manual and hardware-sensitive: Videx
  80-column output, speaker click, GR display / return-to-text, II+ case
  rendering, launcher/editor/browser/Debug feel, and real keyboard editing.
- **Walk:** **0** → **1** (core REPL, history **ABSENT**, redef
  **ERRORS**) → **2** (extras, graphics, memory, speaker clicks, `text80()` on Videx) →
  **4** → **3** (including `,` drive-pick to data disk) → **5** → **8**
- **If time is tight:** run the typed / visual / audio checks and at least
  one graphics sample. Defer the full **7** `TESTS/` + `XTESTS/` sweep
  until the lower-priority automated-suite stage.
- **Stability:** count this toward the **9** Saturn 15-minute requirement.

### Stage 2 - Videx pulled, 40-column fallback

- **Cards:** **PULL Videx** → 40-col; keep Saturn **IN**
- **Boot:** II+ Saturn REPL (`SWIFTSAT`)
- **Why second:** this fallback depends on the physical card state and is not
  meaningfully covered by the automated runner.
- **Walk:** fallback rows only in **0** and **2**: `W` is a no-op,
  `text80()` does nothing, the rig stays 40-column.
- **After this stage:** reinstall Videx before any later 80-column or full
  Saturn+Videx checks.

### Stage 3 - Tier-1 compiler, real ProDOS file path

- **Cards:** Saturn + Videx **IN**
- **Boot:** Tier-1 compiler (`disk-iip-compiler`)
- **Why third:** this exercises the real floppy / ProDOS MLI path, editor
  save+compile+run, Runner Ctrl-C return, file CRUD, and wait/tone timing on
  physical hardware.
- **Walk:** **0** → **3** → **4** (Ctrl-R = save+compile+run) → **5** →
  **6** Tier-1 → **7** `FBTESTS/` [drive 2] → **8** Family-B samples
- **If time is tight:** in `FBTESTS/`, run `tfileio`, `tfiledir`, `twait`,
  and `ttone` first. They depend most on real ProDOS / timing / speaker
  behaviour; the pure language-style Family B tests have better emulator
  coverage.

### Stage 4 - Tier-2 compiler, Saturn-paged bytecode

- **Cards:** Saturn + Videx **IN**
- **Boot:** Tier-2 Saturn compiler (`disk-iip-sat-compiler`)
- **Why fourth:** Saturn paging is hardware-specific, but this is a narrower
  path than the Tier-1 real-file workflow.
- **Walk:** **6** Tier-2 only: compile/run the larger Saturn-paged program
  and confirm graphics / Runner return.

### Stage 5 - Lite disk, no-extras fallback

- **Cards:** keep an LC-compatible card installed. On this bench, the Saturn
  card is also the Language Card, so leave it **IN** and boot the lite disk.
  Only remove the Saturn if a separate 16 KB Language Card is installed in
  its place; ProDOS / `SWIFTIIP` will not start on a 48K-only II+.
- **Boot:** II+ lite REPL (`SWIFTIIP`)
- **Why fifth:** this proves the shipped lite disk and no-extras path still
  boot on the LC-compatible baseline; the core language itself has stronger
  automated and Saturn-REPL coverage.
- **Walk:** **0** lite banner / no-extras path → **1** lite REPL
  reference if it was not already typed on real hardware, otherwise a short
  smoke (`1 + 2`, `:mem`, `:quit`).
- **Stability:** count this toward the **9** lite 15-minute requirement.

### Stage 6 - REPL automated-suite sweep

- **Cards:** Saturn + Videx **IN**
- **Boot:** II+ Saturn REPL (`SWIFTSAT`)
- **Why last:** the on-disk `TESTS/` and `XTESTS/` are still required for a
  full sign-off, but failures here are more likely to have surfaced in the
  emulator / acceptance harness than the manual hardware checks above.
- **Walk:** **7** `TESTS/` + `XTESTS/` [drive 2], expect 0 failures.

**Two standing rules for the whole pass:**

- **Keep the data disk in drive 2 throughout** so the **[DRIVE2]** steps
  run in place.
- **Within one boot, minimise UI-mode changes:** launcher first, then stay
  in each mode while you're in it - REPL work together, then back to the
  launcher for file selector → editor → Debug → samples.

Then complete the **Sign-off** section.

---

## Section 0 - Boot and launcher

For both REPL program disks (II+ lite, II+ Saturn) and, separately, the two
II+ Family B compiler disks:

- [ ] **Boots to the launcher menu** without a monitor trap or garbage
      screen.
- [ ] The menu shows the four options: **1 REPL** (compiler disks: **1
      File Selector**), **2 File Selector**, **3 Debug**, **4 About**.
- [ ] **Menu navigation:** `I` / `M` move the `>` highlight; **Return**, the
      **→ right-arrow**, or a number key activates it. The prompt reads
      `I/M move  Ret/-> or 1-5 select >` (1-4 on a compiler disk).
- [ ] **About (4)** shows the correct version line and the disk-set blurb,
      and the banner is **machine-tagged** correctly:
      - II+ lite → `SwiftII ][+`
      - II+ Saturn → `SwiftII ][+ Saturn`
- [ ] **Launcher text rendering:** lowercase shows as NORMAL uppercase and
      ASCII-uppercase shows as INVERSE. No stray inverse/flashing
      characters.
- [ ] **40/80 toggle `W`** is a no-op on the II+ launcher, even on the Saturn
      disk with **[VIDEX]**. The launcher stays 40-column; exercise Videx from
      the `SWIFTSAT` REPL with `text80()`.

---

## Section 1 - REPL: core language (lite path)

Core REPL behaviour must work on both **II+ lite** (`SWIFTIIP`) and
**II+ Saturn** (`SWIFTSAT`). For the priority hardware pass, type the full
session once during stage 1 on `SWIFTSAT`, then do the lite smoke / fallback
check in stage 5 on `SWIFTIIP`. If the Saturn pass exposes a core-language
failure, rerun this full section on `SWIFTIIP` to isolate lite versus extras.
**[REPL]**.

> **How to use the scripts in this section.** Type each line shown after
> the `> ` prompt **verbatim** and press Return; the line(s) directly
> beneath it are the **exact expected output**. A blank line under an
> input means that input is **silent** (`let` / `var` / assignment / a
> `func` definition print nothing). On the **][+** keyboard, get capitals
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

- [ ] **Backspace / left-arrow (`$08`)** deletes the char to its left.
- [ ] **Line history is correctly ABSENT** on the II+ (`SWIFTIIP` /
      `SWIFTSAT`): the up/down keys do nothing special; only backspace
      edits. (History recall is a //e-only feature - see
      [`TESTING-emulators.md`](TESTING-emulators.md).)

### Function redefinition

- [ ] **Redefinition correctly errors** on the II+ - use `:reset` to
      redefine. (Atomic redefine-in-place is a //e-only feature.)

---

## Section 2 - REPL: extras (Saturn)

Run on the **II+ Saturn** (`SWIFTSAT`) disk. This is the lite surface
**plus** the platform built-ins. **[REPL]**, **[SAT]**.

> **No Saturn extras path:** boot the **II+ lite** disk instead. Keep a
> Language-Card-compatible 64K configuration present: on this bench the Saturn
> card provides that LC role, so do not simply pull it unless you replace it
> with a standard 16 KB Language Card.

All of Section 1 must pass here too; the steps below are the **extras
surface on top**. The typed-script convention is the same (type after
`> `, output shown beneath; blank = silent).

- [ ] Banner reads `SwiftII ][+ Saturn`.
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
      returns to the text screen (reverts cleanly on real HW).
- [ ] **Sound + memory:** typing `poke(49200, 0)` makes one audible
      **speaker click**; `peek(49152)` returns the keyboard byte (≥ 128
      when a key is waiting).
- [ ] **80-column:** type `text80()` (switches to 80 columns via the Videx
      **[VIDEX]**), then `htab(70)` and `print("R")` - the `R` lands at
      **column 70**; `text()` reverts to 40. **PULL Videx → 40-col only**
      (`text80()` is a no-op).

---

## Section 3 - File selector (file-manager mini-UI)

Menu option **2 File Selector** (compiler disks: **1**). Test against a
real ProDOS directory; the **data disk in drive 2** adds `TESTS/` + extra
`SAMPLES/`. **[REPL]** + **[CR]**.

- [ ] **List + highlight:** files show with the per-file gutter; up/down
      (J/K) move the highlight and repaint only the preview.
- [ ] **Text preview pane** shows the highlighted `.swift` contents.
- [ ] **`,` re-picks the drive** - switch to drive 2 (data disk) and back.
      **[DRIVE2]**.
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
**[CR]** (on a CR disk, Ctrl-R = save + compile + run).

- [ ] **Typing** inserts printable ASCII; case renders correctly
      (auto-lowercase to canonical buffer + inverse/normal video).
- [ ] **Ctrl-D** (or the //e **Delete** key) backspaces — deletes the char
      to the left of the cursor.
- [ ] **RETURN** inserts a newline; multi-line editing works (this is the
      multi-line authoring path the REPL lacks).
- [ ] **Cursor movement:** the **← / → arrows** move left/right
      non-destructively (Apple-Pascal style); **Ctrl-O / Ctrl-L** move up/down
      a line (the Apple Pascal pair — also Ctrl-K / Ctrl-J); **Ctrl-A/E** (line
      start/end), **Ctrl-T/V** (page up/down). The cursor stays visible while
      moving. (Movement is fully non-modal — no ESC cursor mode.)
- [ ] **Ctrl-W** types `_` in cooked Swift files; **Ctrl-G** toggles
      cooked (digraph) ↔ raw-text mode (status tag `[DGR]`/`[RAW]`).
- [ ] **Wrapped-line navigation** behaves correctly (II+ single-row-line
      locate).
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
- [ ] **Responsiveness:** typing and cursor-move feel immediate (II+ typing
      + status-on-pause fast paths).

---

## Section 5 - Debug diagnostic (`DEBUG.SYSTEM`)

Menu option **3 Debug** (compiler disks: **2**). Arrows page, **ESC**
exits. Three pages. **[REPL]** + **[CR]**.

- [ ] **Page 1 - VOLUMES:** free / total shown in **both KB and blocks**
      for each online volume. **[SAT]** stands in for the Language Card.
- [ ] **Page 2 - DETECTION:** machine type (the II+ must show **II+**, not
      //e — `$FBB3` reads `$EA`, not `$38`), the explicit **Saturn slot**,
      and the **AUX RAM** readout (moved here from the old MEMORY page;
      shows **N/A (NOT //E)** on the II+).
- [ ] **Page 3 - SLOTS.**
- [ ] All pages are **uppercase-only** (no lowercase via COUT on the II+).
- [ ] **ESC returns** to the launcher cleanly.

---

## Section 6 - Family B: Compiler + Runner

The two II+ compiler disks. Flow: **edit → compile (.swift → .swb) → run**.
**[CR]**. No Saturn card needed to *run* a `.swb`; the tiers differ in how
much **source** the Compiler can handle. **No typing is required for the
end-to-end checks** - each compiler disk ships `SAMPLES/` (e.g.
`GREET.SWIFT`, `FUNCTIONS.SWIFT`, `XSNAKE.SWIFT`, and the Family-B feature
tour `XDICE.SWIFT`); press **`X`** on one to compile + run it. (To exercise
the editor → compile path with your own code, type the Section 4 program
and press **Ctrl-R**, which on a compiler disk = save + compile + run.)

- [ ] **Tier-1 (II+, `disk-iip-compiler`):** `X` on a sample `.swift`
      (e.g. `GREET.SWIFT`) compiles to `.swb` and runs end-to-end; `X` on
      the resulting `.swb` runs it directly without recompiling.
- [ ] **Tier-2 (II+ Saturn, `disk-iip-sat-compiler`):** compile + run a
      **bigger** program - confirm the **Saturn-paged bytecode** driver
      works on iron. **[SAT]**
- [ ] **Ctrl-C breaks** a Runner program parked in `readLine` and returns
      to a readable launcher.
- [ ] **File I/O builtins** (Runner only): `readFile` / `writeFile` /
      append / delete / rename / exists / createDir / deleteDir /
      `listDirectory` round-trip against a real ProDOS data disk (drive 2).
      The exhaustive `FBTESTS/` suite covers these - run them. **[DRIVE2]**.
- [ ] **`for-in` / `switch` / `random`** in a compiled program produce the
      expected output, and **`random` actually varies** run-to-run.
- [ ] **Graphics from a compiled program** draws, then returns to a
      readable launcher.

> The //e Tier-3 (aux-paged bytecode) compiler is out of scope on this rig
> - verify it in [`TESTING-emulators.md`](TESTING-emulators.md).

---

## Section 7 - On-disk automated suites (data disk)

Mount the **data disk** in **drive 2**. These are the scripted suites, run
from the file selector / Runner. **[REPL]** + **[CR]**, **[DRIVE2]**
throughout.

- [ ] **`TESTS/`** (general language) - run, expect 0 failures.
- [ ] **`XTESTS/`** (extras) - run on the II+ Saturn disk, expect 0
      failures.
- [ ] **`FBTESTS/`** (Family B) - run via a compiler disk + data disk on
      drive 2, expect **fail 0** each: `tfileio` (17, file CRUD vs real
      MLI), `tfiledir` (22, directory CRUD), `tswitch` (12), `tforarr` (7),
      `trandom` (9), `twait` (3), `ttone` (3), `tmath` (18).
- [ ] **`twait` shows the delay** - after `wait 1 s ...` / `wait 2 s ...` /
      `wait 3 s ...` it must pause **1, then 2, then 3 seconds** (rising,
      visibly different gaps - the on-target proof `wait()` waited and that
      the delay scales with its argument). If all three feel far too short,
      the ROM-`WAIT` calibration runs fast - note it for a tuning pass.
      `wait()` is Family-B-only - the REPLs reject it ("undeclared name"),
      expected. **[DRIVE2]**.
- [ ] **`ttone` makes sound** - after `blip 1 ...` … `blip 5 ...` / `chirp ...`
      you must **HEAR five rising blips then a low→high chirp** (smaller
      `halfPeriod` = higher pitch - the on-target proof `tone()` toggled the
      `$C030` speaker). This is **the** real-HW audio check (emulators are
      silent), so do it carefully here. `tone()` is Family-B-only - the
      REPLs reject it ("undeclared name"), expected. **[DRIVE2]**.
- [ ] **`tmath` math + string methods** - `abs`/`sgn`/`hasPrefix`/`hasSuffix`,
      expect **`pass 18 fail 0`**. All four are Family-B-only: a Family A REPL
      rejects `abs(-5)`/`sgn(-5)` as "undeclared name" and
      `"x".hasPrefix("y")` as "unknown member", expected. **[DRIVE2]**.

---

## Section 8 - Samples

The `SAMPLES/` set ships on every program disk; the data disk carries the
full set.

- [ ] Each lite sample runs from the file selector: `fib`, `fizzbuzz`,
      `greet`, `strings`, `arrays`, `functions`, `optionals`.
- [ ] Each extras-REPL (`x`-prefixed) sample runs on the II+ Saturn disk:
      `xgraphics`, `xsnake`, `xspeaker`, `xvtab`.
- [ ] Each Family-B-only (`x`-prefixed) sample runs with `[X]` on a compiler
      disk + data disk (and is rejected by every REPL): `xdice`, `xwide`.
- [ ] At least one **GR graphics** demo (`xgraphics` / `xsnake`) renders
      visibly and returns cleanly.

---

## Section 9 - The 30-minute hands-on stability pass

The headline acceptance check on real iron. Fold this into the run order
instead of booting the same disks again: the ordinary Section 1 / 2 / 8 work
counts as Saturn stability time, and the stage-5 lite fallback work counts as
lite stability time. Add extra sample runs only if the stage did not last long
enough.

- [ ] **SWIFTSAT Saturn extras:** about 15 minutes total across the stage-1 /
      stage-2 REPL, extras, graphics, speaker clicks, memory, fallback, and sample
      checks. No monitor traps, garbage characters, or surprising latency.
- [ ] **SWIFTIIP lite:** about 15 minutes total across the stage-5 lite boot,
      core REPL smoke / reference check, and lite samples. No monitor traps,
      garbage characters, or surprising latency.
- [ ] **Fallbacks:** with the Saturn and Videx present first, then the
      documented reduced paths hold: lite disk → no extras while still using
      an LC-compatible 64K setup; Videx pulled → 40-column only.

---

## Sign-off (real II+)

- [ ] **Emulator pre-flight** - [`TESTING-emulators.md`](TESTING-emulators.md)
      green first.
- [ ] **Every section above green** on the real II+; both fallbacks (lite
      disk without extras, Videx pulled) confirmed.
- [ ] **Keyboard matrix** - a full pass of
      [`TESTING-keyboard.md`](TESTING-keyboard.md) (every ✋ and ◐ row ticked).
      This is a required gate step, not optional: the launcher / browser / Debug
      / test-runner key routing is manual *by design* (extraction was measured
      and declined - design doc 019), so this sweep is its test of record.
- [ ] No regressions in the automated suites: `make ci` green.

When this is signed off (and the //e disks are covered in the emulator
pass), the **real-hardware acceptance gate** is met and the v1.0.0 release
(disks staged under `releases/v1.0.0/`) is blessed. The `v1.0.0` git tag is
created manually on GitHub.
