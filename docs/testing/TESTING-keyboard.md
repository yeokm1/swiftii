# TESTING-keyboard.md - every keyboard shortcut, every UI path

This is the **exhaustive keyboard-shortcut matrix**: one row per key, per UI
mode, with what it does and a tick box. It is the companion to the feature-walk
playbooks ([`TESTING-emulators.md`](TESTING-emulators.md) and
[`TESTING-iiplus.md`](TESTING-iiplus.md)) and
to [`TESTING.md`](TESTING.md) (the automated host/sim/integration/REPL suites).
Where the acceptance playbooks walk *features* and happen to press keys, this
document walks *keys* and guarantees none is missed - including the ones that
only matter in Debug, in 80-column mode, or on one machine.

Every row was read off the source and is cited (`file:line` is approximate;
search the symbol if it has drifted). The **Auto** column says how the key is
covered without a human at the keyboard:

- ✅ - covered by a host **unit** test (the pure decision function is unit-tested,
  fast and exhaustive — `make test`).
- ◐ - **partial** - the bug-prone *decision logic* is host-unit-tested, but the
  key *read* is still inline in a cc65 loop.
- ⚙ - **driven end-to-end by the acceptance harness** (`make acceptance`, the
  `keyboard` configs in [`tools/host/acceptance/`](../../tools/host/acceptance/)):
  the real keystroke is injected into izapple2 and the screen is scraped, so the
  whole stack (read → dispatch → render) is exercised, just not as a fast unit
  test. Many rows are **both** ✅/◐ (logic) and ⚙ (end-to-end).
- ✋ - **manual only** - neither host-unit nor harness reaches it; checked on a
  machine or emulator by hand (real-HW-only behaviour, or a flow too awkward to
  script — e.g. the Ctrl-G mode toggle's save-prompt + reload).

> **Why the inline loops aren't host-unit-tested - and why that's fine now.**
> The editor's key logic is a pure function (`editor_dispatch` /
> `editor_cook_key` in [`src/editor/keymap.c`](../../src/editor/keymap.c)), so it
> is host-unit-tested (✅). The launcher menu, volume picker, file selector,
> Debug pager, test runner, and REPL line editor read the keyboard **inline**
> inside their cc65 main loops, so their *decision* isn't a separately-testable
> function — but the **acceptance harness drives them end-to-end** (⚙), so they
> are not untested.
>
> Extracting the inline loops into pure `*_dispatch` functions, the editor
> pattern, is covered in [design doc 019](../contributing/design/019-keyboard-dispatch-extraction.md).
> The //e REPL history ring was extracted (genuine off-by-one bug risk, and it
> fit real headroom). **The launcher / browser / Debug / test runner were
> measured and not:** wiring the launcher to call a tested dispatcher costs
> ~187 B of code, and on the
> launcher's link config BSS overlays the ONCE segment, so the II+ and //e
> launchers overflowed their BSS area (~50 B real slack) - a permanent cost to a
> binary that boots everything, payable only by trimming its C-stack reservation
> (its own regression risk). The manual-sweep cost that argument traded against
> is itself now mostly gone: the acceptance harness (`make acceptance`) auto-runs
> the ⚙ rows. So host-unit extraction stays declined, and `make acceptance` is
> the automated net; a by-hand pass of this matrix is the **backstop** for the
> remaining ✋ rows (real-HW-only behaviour, awkward-to-script flows). Revisit
> host-unit extraction only if a binary frees real BSS-area headroom *and* one of
> these UIs starts changing often.

## How to use this

Pick a disk, boot it, and walk each table for that mode. Tick a box once the key
does what the row says. The **machine/build notes** call out rows that only
exist on one machine (II+ cooked entry, //e history, 80-col). Run it on both
tracks from the acceptance playbook: **Track E** (emulators, `make run-*`) first,
then **Track P** (real Apple II Plus).

**Release gate.** This matrix is a **required step of every release**, not an
optional extra: a full pass - every ✋, ⚙, and ◐ row confirmed, on at least one
emulator track and the real II+ - is part of the v1.0 acceptance gate alongside
the feature-walk playbooks
([`TESTING-emulators.md`](TESTING-emulators.md) / [`TESTING-iiplus.md`](TESTING-iiplus.md)).
The ✅ rows are re-verified for free by `make test`, and the ⚙ rows by `make
acceptance`, on each change; the ✋ rows (and a sanity confirm of ⚙ on real
hardware) are the ones a human must walk. Copy this file (or its tick boxes) per release so
each pass is recorded against its build.

Key-code conventions: Apple arrow keys *are* control codes - left = Ctrl-H
(`$08`), right = Ctrl-U (`$15`), up = Ctrl-K (`$0B`), down = Ctrl-J (`$0A`).
A byte therefore can't tell "arrow" from "that Ctrl-letter"; both are listed.

---

## 1 - Boot launcher: main menu

`tools/apple2/boot_launcher/boot_launcher.c` (menu loop ~2254). Lowercase letters are
folded to uppercase and the high bit is masked, so `i`/`I`/`$C9` are the same
key. The menu has **5 options on a REPL disk** (1 REPL · 2 File Selector ·
3 Debug · 4 Run tests · 5 About) and **4 on a Family B compiler disk** (the REPL
option is dropped; 1 File Selector · 2 Debug · 3 Run tests · 4 About).

| Key | Action | Auto | ✓ |
|-----|--------|------|---|
| `I` or Up-arrow (`$0B`) | move the `>` highlight up | ⚙ | [ ] |
| `M` or Down-arrow (`$0A`) | move the highlight down | ⚙ | [ ] |
| `Return` (`$0D`) or right-arrow (`$15`) | activate the highlighted option | ⚙ | [ ] |
| `1`–`9` | pick an option by its on-screen number | ⚙ | [ ] |
| `D` | jump straight to **Debug** (`DEBUG.SYSTEM`) | ⚙ | [ ] |
| `A` | jump straight to **About** | ⚙ | [ ] |
| `W` | toggle 40/80 column - **//e disk only** (`LITE_IIE`); no-op on II+ | ⚙ | [ ] |
| any other key | re-show the menu (harmless) | ✋ | [ ] |

**Notes.** On a Family B disk the digit range is 1–4 and there is no REPL
option. The `W` row is compiled out of the II+ launcher, so on a II+ it does
nothing even with a Videx (the launcher itself is always 40-col). Unlike Debug
(`D`) and About (`A`), **Run tests has no direct-jump hot-key** - pick it by its
menu number or with Return / right-arrow on the highlight (there is no `T`
shortcut). The on-screen prompt reads `I/M move  Ret/-> or 1-5 select >`.

---

## 2 - Boot launcher: volume / drive picker

`tools/apple2/boot_launcher/boot_launcher.c` (~1281). Shown when you enter the File
Selector (it asks which volume first) - this is where you switch to **drive 2 /
the data disk**.

| Key | Action | Auto | ✓ |
|-----|--------|------|---|
| `I` or Up-arrow (`$0B`) | highlight previous volume | ⚙ | [ ] |
| `M` or Down-arrow (`$0A`) | highlight next volume | ⚙ | [ ] |
| `Return` (`$0D`), `$15`, or `.` | open the highlighted volume | ⚙ | [ ] |
| `Q` or `Esc` (`$1B`) | back to the main menu | ✋ | [ ] |

---

## 3 - File selector / browser

`tools/apple2/boot_launcher/boot_launcher.c` (~1750). Two-pane: file list (left) +
text preview (right; full-width 80-col on //e). All actions hit the **real
ProDOS directory** - verify each persists by re-listing.

| Key | Action | Auto | ✓ |
|-----|--------|------|---|
| `M` or Down-arrow (`$0A`) | move the highlight down (repaints preview only) | ⚙ | [ ] |
| `I` or Up-arrow (`$0B`) | move the highlight up | ⚙ | [ ] |
| `Ctrl-T` (`$14`) | page the file list up | ⚙ | [ ] |
| `Ctrl-V` (`$16`) | page the file list down | ⚙ | [ ] |
| `J` | scroll the **preview** pane up | ⚙ | [ ] |
| `K` | scroll the **preview** pane down | ⚙ | [ ] |
| `Return` (`$0D`), `$15`, or `.` | open file in editor / launch a `.SYSTEM` / enter a folder | ✋ | [ ] |
| left-arrow (`$08`) or `,` | up a directory / re-pick the drive | ✋ | [ ] |
| `X` | run a `.SWIFT` (REPL disk) or compile+run a `.swb` (Family B) | ✋ | [ ] |
| `E` | edit the selected file | ✋ | [ ] |
| `F` | new (empty) file | ⚙ | [ ] |
| `R` | rename the selected file (text prompt) | ✋ | [ ] |
| `D` | delete the selected file (then `Y` to confirm) | ✋ | [ ] |
| `N` | new folder / `mkdir` (text prompt) | ✋ | [ ] |
| `W` | toggle 40/80 column - **//e only** | ✋ | [ ] |
| `Q` or `Esc` (`$1B`) | back to the menu | ⚙ | [ ] |

**Prompt keys** (rename / new-file / new-folder text entry): `Return` accept ·
`Esc` cancel · backspace · printable characters.
**Delete confirm**: `Y` deletes, any other key cancels.

---

## 4 - Editor

The editor is merged into the launcher (`src/editor/editor.c`) and its key logic
is the pure `src/editor/keymap.c`. **This is the most automated UI path** - the
buffer/cursor/action keys are host-tested in
[`tests/editor/keymap_test.c`](../../tests/editor/keymap_test.c).

### 4a - Editing & cursor (host-tested via `editor_dispatch`)

| Key | Action | Auto | ✓ |
|-----|--------|------|---|
| printable ASCII | insert at the cursor; sets dirty | ✅ ⚙ | [ ] |
| Ctrl-D (`$04`); Delete (`$7F`) | backspace (delete-left) | ✅ | [ ] |
| `Return` (`$0D`) | insert a newline | ✅ ⚙ | [ ] |
| left-arrow / Ctrl-H (`$08`) | non-destructive move left | ✅ ⚙ | [ ] |
| right-arrow / Ctrl-U (`$15`) | non-destructive move right | ✅ ⚙ | [ ] |
| up-arrow / Ctrl-K (`$0B`) / Ctrl-O (`$0F`) | line up (Ctrl-O = Apple Pascal) | ✅ ⚙ | [ ] |
| down-arrow / Ctrl-J (`$0A`) / Ctrl-L (`$0C`) | line down (Ctrl-L = Apple Pascal) | ✅ ⚙ | [ ] |
| Ctrl-A (`$01`) | line start | ✅ ⚙ | [ ] |
| Ctrl-E (`$05`) | line end | ✅ ⚙ | [ ] |
| Ctrl-T (`$14`) | page up (viewport follows) | ✅ | [ ] |
| Ctrl-V (`$16`) | page down | ✅ | [ ] |
| other control bytes (Tab `$09`, Ctrl-C `$03`, …) | ignored | ✅ | [ ] |

### 4b - I/O actions (host-tested action return; loop does the I/O)

| Key | Action | Auto | ✓ |
|-----|--------|------|---|
| Ctrl-S (`$13`) | save (prompts `SAVE AS:` when untitled) | ✅ | [ ] |
| Ctrl-R (`$12`) | run (REPL disk) / save+compile+run (Family B disk) | ✅ | [ ] |
| Ctrl-Q (`$11`) | quit to the browser (confirms unsaved edits; open another file from there) | ✅ ⚙ | [ ] |

### 4c - Loop-layer keys (host-tested helpers, wired in the cc65 loop)

| Key | Action | Auto | ✓ |
|-----|--------|------|---|
| Ctrl-W (`$17`) - **II+ cooked** | inserts `_` (the II+ keyboard has no `_` key) (`editor_cook_key`) | ✅ ⚙ | [ ] |
| Ctrl-G (`$07`) - **II+** | toggle cooked (digraph) ↔ raw mode; status tag flips `[DGR]`/`[RAW]` | ✋ | [ ] |
| Ctrl-W (`$17`) - **//e** (`LITE_IIE`) | toggle 40 ↔ 80 column | ✋ | [ ] |
| typed `A`–`Z` - **II+ cooked** | auto-lowercase into the canonical buffer (`editor_cook_key`) | ✅ ⚙ | [ ] |

**Prompt keys** (`SAVE AS:` / rename): `Return` accept · `Esc` cancel ·
backspace (`$08`/`$7F`) · printable (auto-uppercased in the prompt).
**Save-on-quit confirm**: `Y` save · `N` discard · `Esc` or `C` cancel.

**Notes.** Section 4c "II+" rows are `ED_PRE_IIE`-gated. On a //e the buffer is
already canonical case so `editor_cook_key` is not applied and `$15` moves right.
Up/down on a real ][+ (no up/down arrow keys) is Ctrl-O / Ctrl-L (Apple Pascal)
or Ctrl-K / Ctrl-J.

---

## 5 - REPL line editor

`src/platform/apple2/keyboard.c` `platform_read_line`. Inline `cgetc` loop, so
the key *reads* are ✋. Behaviour differs by build: **II+** REPLs canonicalise
the line on `Return` and have **no history**; **//e** REPLs (`SWIFTIIE` /
`SWIFTAUX`) add the history ring; the **Runner** adds Ctrl-C break.

The //e history **recall logic** - the ring, the de-dup, and the
park/restore nav index (the off-by-one-prone part) - was extracted into the
pure `histring.{c,h}` and is now host-tested (`tests/platform/histring_test.c`,
design doc 019), so the recall rows below are ◐: logic ✅, key read still ✋ (the
`cgetc` dispatch stays inline in `platform_read_line`). Extracting the shared
line keys so the II+ REPL binaries *call* a tested dispatcher is **blocked by
budget** - SWIFTSAT has ~36 B of MAIN headroom, not enough for a called
extraction - so those rows stay ✋ and are walked here by hand (design doc 019).

| Key | Action | Build | Auto | ✓ |
|-----|--------|-------|------|---|
| printable | insert + echo | all | ⚙ | [ ] |
| `Return` (`$0D`) | submit line (II+ runs `input_translate` first) | all | ⚙ | [ ] |
| Backspace (`$08`) / Delete (`$7F`) | delete-left (destructive `BS SPC BS` echo) | all | ⚙ | [ ] |
| Ctrl-W (`$17`) | insert `_` (input-method underscore; same key as the editor) | II+ only (`!WITH_IIE`) | ⚙ | [ ] |
| right-arrow / Ctrl-U (`$15`) | ignored (no in-line cursor; it no longer types `_`) | II+ | ✋ | [ ] |
| Ctrl-D (`$04`) on an **empty** line | EOF → exit REPL to launcher | all | ⚙ | [ ] |
| up-arrow (`$0B`) / Ctrl-P (`$10`) | recall older history line | **//e REPL only** | ◐ ⚙ | [ ] |
| down-arrow (`$0A`) / Ctrl-N (`$0E`) | newer line; past newest restores the in-progress line | **//e REPL only** | ◐ ⚙ | [ ] |
| Ctrl-C (`$03`) | break a program parked in `readLine` | **Runner only** | ✋ | [ ] |

**Notes.** History is an 8-deep ring (`HIST_SLOTS`); an exact repeat of the last
line and over-long lines are not recorded, so a recalled line is byte-exact.
On II+ the up/down keys do nothing special (history is absent) - verify that.

### 5b - REPL meta-commands (`src/repl/metacmds.c`)

| Command | Action | Auto | ✓ |
|---------|--------|------|---|
| `:help` | list the commands | (REPL suite) ⚙ | [ ] |
| `:mem` | used / free heap bytes | (REPL suite) ⚙ | [ ] |
| `:list` | list current bindings in definition order | (REPL suite) ⚙ | [ ] |
| `:reset` | clear bindings + funcs, reset the heap | (REPL suite) ⚙ | [ ] |
| `:quit` | exit to the launcher (cold-reboot-to-menu) | (REPL suite) | [ ] |

---

## 6 - Debug diagnostic (`DEBUG.SYSTEM`)

`tools/apple2/debug_sys/debug.c` (~339). Three pages (VOLUMES · DETECTION ·
SLOTS); redraw only on a page change. Uppercase-only on II+ (COUT). (The
MEMORY page was dropped — its free-page counts only measured DEBUG.SYSTEM's
own footprint; its AUX RAM readout moved to DETECTION.)

| Key | Action | Auto | ✓ |
|-----|--------|------|---|
| right-arrow (`$15`) | next page (stops at page 3) | ⚙ | [ ] |
| left-arrow (`$08`) | previous page (stops at page 1) | ⚙ | [ ] |
| `Esc` (`$1B`) | exit, chain back to the launcher | ⚙ | [ ] |
| any other key | ignored (no disk re-read) | ✋ | [ ] |

---

## 7 - Test runner (`TESTRUN.SYSTEM`)

`tools/apple2/testrun_sys/testrun.c`. Selection checklist appears only when the disk has
≥2 test tiers.

| Key | Action | Where | Auto | ✓ |
|-----|--------|-------|------|---|
| `1`,`2`,… | toggle a test tier on/off | selection screen | ✋ | [ ] |
| `Return` (`$0D`) or right-arrow (`$15`) | run the selected tiers (no-op if none selected) | selection screen | ⚙ | [ ] |
| `Esc` (`$1B`) | back to the launcher | selection / results | ✋ | [ ] |
| any other key | skip the remaining inter-test wait | during the sweep | ✋ | [ ] |

---

## 8 - 80/40-column paths (cross-cutting)

The 40↔80 toggle surfaces in several modes; gather them here so none is missed.
Driver: `src/platform/apple2/screen.c` (host-tested where pure:
[`tests/editor/screen_test.c`](../../tests/editor/screen_test.c)).

| Where | Key / call | Machine | ✓ |
|-------|-----------|---------|---|
| Launcher menu | `W` | //e firmware 80-col; II+ launcher stays 40 | [ ] |
| File selector | `W` | //e | [ ] |
| Editor | Ctrl-W | //e (`LITE_IIE`) 80-col; on II+ Ctrl-W types `_` and Ctrl-G is the cooked/raw toggle | [ ] |
| REPL / program | `text80()` then `text()` | //e firmware; II+ Videx in `SWIFTSAT` (Saturn + Videx) or Family B Runner | [ ] |

**PULL Videx -> 40-col only**: on a II+ `text80()` falls back to (or stays at)
40 columns; the launcher `W` key is already a no-op on II+. **izapple2 quirk**:
`text()` does not revert 80->40 (real HW does). See the acceptance playbook's
"Known emulator-only quirks".

---

## Coverage summary

- **✅ Host-unit-tested:** the entire editor key surface - buffer/cursor/action
  keys (`editor_dispatch`) and the II+ cooked-mode mapping (`editor_cook_key`).
  Run with `make test`.
- **◐ Logic host-tested, key read inline:** the //e REPL history recall - the
  ring + de-dup + park/restore nav index is host-tested in
  [`tests/platform/histring_test.c`](../../tests/platform/histring_test.c) (design
  doc 019); only the `cgetc` read stays inline (and is ⚙ end-to-end below).
- **⚙ Driven end-to-end by the acceptance harness (`make acceptance`):** launcher
  menu nav, volume picker, file browser nav, the editor (type / Ctrl-A·E / ←·→ /
  Ctrl-O·L / Ctrl-W — II+ `_` insert · //e 40↔80 width toggle, the latter also
  asserting the post-toggle directory re-read still lists a real entry (the JSR
  $C300 MLI fix) / Ctrl-Q), the REPL (`1+2`, `:help`/`:list`/`:mem`/`:reset`,
  backspace, Ctrl-W, Ctrl-D exit, //e history), and the Debug pager. The harness
  injects the real keystroke into izapple2 and scrapes the screen, so the inline
  cc65 loops *are* exercised automatically - host-unit extraction (doc 019) was
  **measured and declined** on byte cost, and the harness covers the gap the
  extraction would have.
- **✋ Manual only:** what neither layer reaches - real-HW-only behaviour, and
  flows too awkward to script (the Ctrl-G cooked/raw toggle's save-prompt +
  reload, browser rename/delete, the test runner). A by-hand pass of this matrix
  is the **backstop** for those and a required release-gate step (see
  [How to use this](#how-to-use-this)).
- The REPL meta-commands and language surface are additionally covered by the
  `tests/repl/` scripts and the on-disk `TESTS/` suites (see `TESTING.md`).
