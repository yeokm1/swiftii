# 006 - The text editor

SwiftII's on-target text editor lets a stock Apple II user author, save,
and run multi-line `.swift` programs without leaving the machine. It ships
as an in-process subsystem of the boot launcher (`SWIFTII.SYSTEM`): the
launcher's file browser calls `editor_main(path)` and the editor returns
when the user quits. See `src/editor/` in
[PROJECT_LAYOUT.md](../PROJECT_LAYOUT.md).

## Problem

A SwiftII user on an Apple II Plus would otherwise have exactly one way
to enter code: line by line at the REPL prompt. The REPL is fine for
exploration and single statements, but writing a multi-line program -
define functions, iterate on them, run the whole thing - needs a real
editor. The alternatives (author on a Mac and transfer a disk image;
retype at the REPL each session; go through Applesoft's line editor with
no conversion tooling) all fail v1.0's promise that the language is
**usable on a real Apple II Plus**.

The editor is also where the typing model (design doc 003) gets its full
workout: the REPL hits the input layer one line at a time, but the
editor hits it on every keystroke, in every context (strings, comments,
identifiers), with the case-marker and digraph rules all active.

## Constraints

- Runs on an Apple II Plus with a 16K language card (64K total).
- Produces canonical lowercase ASCII `.swift` files (design doc 003).
- Reuses the existing platform layer; never duplicates the
  lexer/compiler/VM.
- Respects CONSTRAINTS.md: no float, no malloc, no printf, no deep
  recursion, no high-level stdio in tight loops.

## Where it lives: a launcher subsystem

The editor links straight into the boot launcher binary
(`SWIFTII.SYSTEM`): the launcher already keeps ProDOS MLI resident, which
is all the editor needs for file I/O, so it runs as part of the launcher
rather than as its own SYS file.

Both the launcher and the editor are **MAIN-only** - neither maps the
language card - so ProDOS's MLI body stays live. That single fact is
what lets the editor read and write files (Ctrl-S, save), lets the
launcher chain an interpreter, and lets **Ctrl-Q return to the file
browser with no cold reboot** (the interpreters, by contrast, map the LC
over MLI and must cold-reboot to get back - see
`screen.c::platform_shutdown`).

Modules (`src/editor/`):

| File | Role |
|------|------|
| `gapbuf.c` | gap-buffer text model: insert / delete / move |
| `textnav.c` | cursor + logical-line navigation over the buffer |
| `screen.c` | builds the pure status / work / message grid |
| `keymap.c` | key → buffer/cursor command dispatch |
| `fileio.c` | load / save `.swift` files via raw ProDOS MLI |
| `editor.c` + `editor_asm.s` | platform loop: read key → dispatch → render → blit |

The editor links these plus `input.c`; it does **not** link the lexer,
compiler, VM, runtime, REPL, or file_runner - it edits bytes and writes
them to disk. The portable modules (gapbuf / textnav / screen / keymap /
fileio) are host-compiled and unit-tested in `tests/editor/`.

## Process model and handoffs

`editor_main(path, start_cursor)` runs the edit loop and returns a code
to the launcher:

- **Browser → editor.** Opening a file (`E` / `RET` in the browser)
  calls `editor_main(path)` in-process: it opens the file via MLI into
  the gap buffer, puts the cursor at top-left, draws the screen, and
  enters the loop. A `NULL`/empty path is an untitled scratch buffer
  that prompts for a name on first save.
- **Ctrl-Q → browser.** `editor_main` returns; the launcher redraws the
  browser. No reboot, no chain.
- **Ctrl-S.** Save to the current file (prompts `SAVE AS:` when untitled).
  (Opening a *different* file is done from the browser — Ctrl-Q back, then
  pick it — so the editor has no in-place open command.)
- **Ctrl-R → save and run.** The editor saves the buffer (and its cursor
  offset), then returns `EDITOR_RUN`. The launcher reads
  `editor_saved_path()`, stages the source for a run (program bytes at
  `$0C00` / `STAGED_SRC_ADDR`, length at `$1B06` / `STAGED_LEN_ADDR`),
  and chains the capability-selected interpreter (lite vs extras - the
  same pick the launcher makes for a plain run). The interpreter runs the
  staged program once, then drops to the REPL; returning to the launcher
  menu is the REPL's `:quit` cold-reboot. On the next edit of that file
  the launcher passes `editor_saved_cursor` back, so an edit → run → edit
  round-trip restores the cursor position.

**UX consequence:** REPL session state (globals, defined functions) is
**not** preserved across a Ctrl-R run cycle - the interpreter is a fresh
session, and the trip back through the launcher cold-reboots. This is
documented in `LANGUAGE.md`'s REPL section and in `:help`; v1 accepts it.

## In-RAM text model: gap buffer

The editor stores the file being edited as a **gap buffer**: a
contiguous byte array with a movable hole. Insertions at the cursor push
bytes into the gap; deletions pull the gap edges apart; cursor moves
slide bytes across the gap.

```
Gap buffer layout (gap between cursor and rest of text):

  ┌──────────────────┬─────────────────┬─────────────────────┐
  │ text before cur  │      GAP        │  text after cursor  │
  └──────────────────┴─────────────────┴─────────────────────┘
  buf_start         pre_end           post_start         buf_end

  Logical buffer = pre + post
  Logical length = (pre_end - buf_start) + (buf_end - post_start)
  Gap size       = post_start - pre_end
```

Operations are O(1) for insert/delete at the cursor, O(distance) for
cursor moves (sliding bytes across the gap). For human-rate typing this
is plenty fast on a 1 MHz 6502.

**Sizing.** The gap buffer is a fixed low-RAM window (`GAPBUF_CAP`, the
`$0800-$1BFF` GAPBUF region the launcher hands to the editor). Because
the launcher is RAM-full, the buffer is modest - roughly 3 KB on the II+
build, 4 KB on the //e build. A file larger than the buffer is **refused
with "FILE TOO BIG TO EDIT"** rather than truncated; such programs are
authored on a host and run from disk via the file selector. Files are
read and written once per open/save through the shared raw-ProDOS-MLI
layer, never per keystroke.

## Screen layout

40-column text mode on //+ and IIe stock (80-column on //e in a later
slice). The 24-row screen is three regions:

```
40-col layout:

  ┌────────────────────────────────────────┐
  │ * foo.swift  [DGR]            line 12  │  ← status line (row 0)
  ├────────────────────────────────────────┤
  │ let counter = 0                        │  ← work area (rows 1-22)
  │ let limit   = 100                      │
  │                                        │
  │ func step(_ n: Int) -> Int <%          │     22 rows × 40 cols
  │    return n + 1                        │
  │ %>                                     │
  │ ...                                    │
  ├────────────────────────────────────────┤
  │ Save as: greet█                        │  ← message line (row 23)
  └────────────────────────────────────────┘
```

- **Status line (row 0):** filename, dirty indicator (`*` prefix when
  the buffer has unsaved changes), the `[DGR]`/`[RAW]` mode tag, and the
  current line number.
- **Work area (rows 1-22):** the gap buffer's contents around the cursor.
  Lines longer than 40 columns wrap with a continuation marker in column
  40.
- **Message line (row 23):** command prompts (`Save as:`),
  confirmations (`Saved.`), and errors (`Disk full.`).

## Rendering

Letters render via the design-doc-003 pre-IIe path: lowercase as
**normal** uppercase, capitals as **inverse** uppercase; `{` `}` `|`
render as their digraph forms. On //e and later, native rendering. This
is **byte-for-byte the same rendering the launcher's file preview uses**
(`preview_putc` in `boot_launcher.c`), so a `.swift` file looks identical
in the selector and in the editor.

The editor builds its grid in `screen.c`, then writes Apple **screen
codes straight to video RAM** (`editor.c` `screen_code` / `blit_row`)
rather than routing the work area through cc65's `cputc`. Reason: cc65's
`cputc` interprets `$0A`/`$0D` as LF/CR, which collide with the
inverse-letter screen codes for `j` (`$6A-$60=$0A`) and `m`
(`$6D-$60=$0D`) - a `cputc` blit garbles those letters and can strand the
inverse cursor block over an `m`/`j` cell. Direct writes also never
scroll and make the blinking cursor a single deterministic store. The
screen-code mapping (capitals → inverse, lowercase → normal video)
mirrors `emit()`'s pre-IIe rules.

## Cooked / raw mode

The Swift rendering and the typing transforms apply in **cooked (digraph)
mode**. A plain text file (any non-`.swift` name, e.g. the on-disk
`README.TXT` help) is loaded, shown, and saved **verbatim** in **raw
mode**: no case markers / auto-lowercase, no inverse swap, no digraph
expansion.

The mode is not fixed by the filename. `editor_path_is_swift` only sets
the **default at load** (`.swift` and untitled scratch → cooked;
anything else → raw); on a //+ build the user flips it at runtime with
**Ctrl-G** (it moved off Ctrl-W → Ctrl-L → Ctrl-G as those keys were claimed
for `_` and then move-down; the //e build keeps Ctrl-W for its 80-col
fallback). The flag
is `s_cooked` in `editor.c`, threaded into `editor_file_load` /
`editor_file_save` as the `cooked` argument. Because the gap buffer's
byte representation differs between modes (cooked holds *input form* -
lowercase + `'` markers + literal `<%`; raw holds bytes as-is), the
toggle **reloads the file from disk** in the new mode (confirming any
unsaved edits first) so display/entry/save stay consistent. The status
row shows the active mode as a `[DGR]`/`[RAW]` tag after the filename
(`stamp_mode_tag`, stamped post-render so the pure renderer and its tests
stay unchanged), and a `^G MODE` hint advertises the toggle. On real //+
hardware raw mode shows lowercase and `{ } |` through the limited
character ROM (as their uppercase / absent glyphs) - the accepted "as-is"
view.

## Keyboard bindings

| Key | Action |
|-----|--------|
| Letter / digit / punctuation | Insert at cursor (via input layer) |
| Left arrow / Ctrl-H | Move cursor left one byte (non-destructive) |
| Right arrow / Ctrl-U | Move cursor right one byte (non-destructive) |
| Up arrow / Ctrl-K / Ctrl-O | Move cursor up one logical line (Ctrl-O = the Apple Pascal "up") |
| Down arrow / Ctrl-J / Ctrl-L | Move cursor down one logical line (Ctrl-L = the Apple Pascal "down") |
| Ctrl-A | Move to start of line |
| Ctrl-E | Move to end of line |
| Ctrl-D / Delete (`$04`, `$7F`) | Backspace (delete the char to the left) |
| Ctrl-T / Ctrl-V | Page cursor up / down |
| Return (`$0D`) | Insert newline at cursor |
| **Ctrl-S** | Save buffer to current filename |
| **Ctrl-R** | Save buffer, then run it (hands a run request back to the launcher) |
| **Ctrl-Q** | Return to the launcher browser (prompts save if dirty) |
| **Ctrl-W** | II+ cooked Swift mode: insert `_` (the II+ keyboard has no `_` key) · //e: 40↔80-col fallback |
| **Ctrl-G** | //+: toggle cooked (digraph) ↔ raw mode, reloading the file |

**Arrows are non-destructive motion (Apple Pascal lineage).** The Apple
][+ keyboard has only ← / → (no up/down) and they *are* Ctrl-H / Ctrl-U at
the hardware level. Following the Apple Pascal (UCSD) editor, both arrows
move the cursor without deleting; deletion is the separate Ctrl-D / Delete
backspace. The ][+ has no up/down arrow keys, so up/down follow Apple
Pascal's **Ctrl-O (up) / Ctrl-L (down)** — O sits directly above L on the
keyboard. Freeing those keys cascaded: in-editor open-by-name was dropped
(files open from the browser), vacating **Ctrl-O**; and the cooked/raw mode
toggle — which had moved Ctrl-W → Ctrl-L when Ctrl-W became `_` — moved on to
**Ctrl-G** so Ctrl-L could be move-down. (The //e build keeps Ctrl-W for its
40↔80-col fallback and types `_` directly.) Forward-delete was dropped: it is
just → then Ctrl-D. The REPL prompt is unchanged — its append+backspace line
editor stays as-is (the at-ceiling II+ interpreters have no room for in-line
cursor editing).

**Movement is entirely non-modal.** Left/right is the ← / → arrows; up/down is
Ctrl-O / Ctrl-L (Apple Pascal) or Ctrl-K / Ctrl-J (the arrow byte codes). The
emacs-style Ctrl-B/F and Ctrl-P/N aliases were removed, and so was the
**ESC-IJKM cursor diamond** (the Apple-ROM convention): it was redundant once
up/down had direct keys, and being modal it had a footgun — while it was active
a typed `i`/`j`/`k`/`m` moved instead of inserting. (It briefly lived as a
sticky mode with an on-screen banner before being dropped; ESC now does nothing
in the edit loop, only cancelling prompts/dialogs.)

**Find / find-replace was planned but not shipped** - the implementation
was dropped to keep the launcher+editor image inside the II+ budget.
**Tab** is not supported (no tab key on the //+ keyboard); indentation is
spaces only.

## Cursor over multi-display-character bytes

On pre-IIe the character `{` displays as `<%` (two screen columns) but is
one byte in the buffer. The cursor moves per **byte** when arrowed
left/right (so `<` `{` `>` is three cursor positions even though the
display shows four columns). The column-to-byte mapping is computed when
redrawing the work area: each rendered byte counts its display width (1
for most, 2 for `{` `}`, 3 for `|`; the digraph-aware `ed_disp_col`). On
//e and later, every byte is one display column and this path is dormant.

## Error handling

| Situation | Editor response |
|-----------|-----------------|
| Disk full on save | Show `Disk full.`; buffer stays dirty |
| Buffer full (gap buffer at capacity) | Show the buffer-full message; reject the insert |
| File larger than the gap buffer on open (from the browser) | Show `FILE TOO BIG TO EDIT`; do not truncate |
| Filename longer than the ProDOS limit (15 chars) | Show `Name too long.`; reject |
| Read I/O error on load (from the browser) | Show `Read error.`; buffer unchanged |
| Write I/O error mid-save | Show `Write error.`; buffer stays dirty |

No editor action should leave the user in a broken state. The buffer is
always the source of truth; nothing in the editor's flow throws data away
silently.

## Responsiveness - fast paths differ by disk

The per-keystroke cost is the full 24-row grid rebuild + diff (not the
byte scan), so a post-Phase-14 pass added per-keystroke fast paths.
**These are not uniform across the two disks**, because the II+ launcher
build has far less free space than the //e one:

| Editor fast path | //e disk | II+ disk |
|---|---|---|
| Backspace (Ctrl-D / `$7F`) | yes | yes |
| Cursor-move fast path (arrows incl. ← now move) | yes | yes (single-row lines) |
| **Typing fast path** (re-render only the edited line) | yes (wrap-aware) | yes (wrap-aware) |
| **Status-on-pause L/C refresh** | yes | yes |

**Typing fast path** (II+ landed in Phase 16, wrap-aware on both disks):
a printable insert that leaves the edited line's screen-row count
unchanged re-renders just that line's row(s) via the shared
`editor_render_wrapped` and blits them, instead of the whole 24-row grid
+ diff. The row count uses the digraph-aware `ed_disp_col`, so the arming
matches how the renderer lays the line out (correct for wrapped
`{`/`}`/`|` lines too). An insert that spills the line onto a new row
changes the count and falls through to a full render. The **cursor-move**
fast path on the II+ stays restricted to single-row lines - a digraph
wrap-arithmetic correctness limit, not a budget one.

**Status-on-pause L/C refresh** recomputes the cursor's line/column from
the live buffer when idle (the II+ arm re-stamps the `[DGR]`/`[RAW]` tag
the refresh overwrites), so the readout stays still while you type/move
fast and catches up the moment you pause.

Funding both II+ fast paths meant clawing back launcher BSS: the
in-launcher Debug screen moved out into a standalone `DEBUG.SYSTEM`, and
the volume-picker disk-space readout was dropped (later re-added inline on
the //e launcher and via `DEBUG.SYSTEM` on the II+). See
`docs/contributing/ROADMAP.md` Phase 16 for the full record.

## Status

Shipped as Phase 14, in-process in the boot launcher (depending on the
design-003 typing model and the Phase 8 hardware-capability detection).
It is verified by the host `tests/editor/` fixtures (`gapbuf`, `textnav`,
`screen`, `keymap`, `fileio`, `session`) and a real-hardware pass on a
stock Apple II Plus (the Phase 14 acceptance block in ROADMAP.md).
