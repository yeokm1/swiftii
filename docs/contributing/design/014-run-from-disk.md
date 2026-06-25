# 014 - Run Swift programs from disk (Phase 13)

This doc records the realized run-from-disk design and the option-3
Compiler/Runner split that later became the Family B toolchain.

## Goal

Let a user pick and run a saved `.swift` program from disk (and, later,
edit one), on every Apple II the project targets, in addition to the
interactive REPL.

## Hard constraints (why the design looks the way it does)

1. **ProDOS passes no argv to a chained SYS program.** The file/path must
   come from a runtime source (the boot launcher or the REPL), not the OS.
2. **The interpreter has almost no code budget.** The gate (slice 0)
   showed ProDOS MLI file I/O (~320 B+) overflows the II+ lite binary
   (SWIFTIIP, ~77 B headroom) and even the bare wrapper busts it; SWIFTSAT
   MAIN sits at ~4 B. So the interpreter cannot read files itself.
3. **The extras chain loaders use `$0800–$17FF` as copy-down scratch**
   during boot, so a pre-staged source there is clobbered on SWIFTSAT /
   SWIFTAUX.

## Realized architecture (shipped)

- **The boot launcher is the launcher.** Explicit menu (no auto-countdown).
  Since the 5-disk split each program disk carries exactly ONE interpreter, so
  the lite/extras menu split collapsed to a single REPL: `1` REPL · `2` File
  selector · `3` Debug · `4` About · `Return` or `→` (right-arrow) = the
  highlighted option (I/M move the `>`; right-arrow matches the file/volume
  pickers). (A `Help` menu screen lived here until 2026-06-15;
  it moved to an on-disk `README.TXT` - opened from the File selector / editor -
  to reclaim launcher code space, and About points the user there.) The top
  line shows the disk's interpreter banner
  (the disk's "build name", e.g. `SwiftII ][+ Saturn`), discovered at startup by
  probing which binary is on the volume (file_on_disk).
- **Staged-source handoff.** The launcher reads the chosen `.swift` via MLI
  into `$0C00` (`STAGED_SRC_ADDR`) and deposits the byte count at `$1B06`
  (`STAGED_LEN_ADDR`, in the `$1B00` launcher→interpreter handoff page next to
  the Saturn slot at `$1B04`). Both survive the lite chain's `$2000+`
  READ. The interpreter's `repl_run` startup hook compiles straight from
  `$0C00` (no source buffer, ~59 B) and runs it once, then drops to the
  REPL. No MLI and no `:load` live in any interpreter binary.
- **Running a file.** The staged source is machine-agnostic (every binary reads
  `STAGED_LEN_ADDR` at startup). With one interpreter per disk, there is no
  lite-vs-extras choice: in the browser, **`[X]` executes** the highlighted
  `.swift` with this disk's sole interpreter (`Return` / right-arrow OPENS it in
  the editor instead, or launches a `.SYSTEM` file). The launcher chains the binary
  present on the volume regardless of the HW probe (bidirectional lite↔extras
  fallback in `main`).

## Plan - the File selector: file-manager mini-UI

Discoverability: the File selector (menu option `2`) opens a minishell that **lists** the current
directory's `.swift` programs + subdirectories and lets the user act on a
**highlighted** entry, instead of typing a name blind. This is a small
interactive file manager, not a command line.

### Two-pane layout

The 40-column browser is a **two-column view** (Norton-Commander style,
fits both the II+ 40-col and the //e - the boot launcher renders in 40-col on
both):

```
DIR: /SWIFTII/SAMPLES/
PARENT 1          CURRENT 1/14           <- per-pane position
* /SAMPLES        > FIZZBUZZ.SWIFT
                    FIB.SWIFT
                    ARRAYS.SWIFT
FIZZBUZZ.SWIFT  TXT  304 B
   1 for i in 1...100 <%                 <- preview pane (numbered lines;
   2   if i %% 15 == 0 <% print(...)        scroll with J/K)
   3 ...
LINE 1-9/23                              <- visible source lines / total
I/M ^T/V=pg J/K=scrl ,=up .=in Ret=open
[X]ec [E]dit [F]new [R][D][N] [Q]uit
```

- **Left pane** = the parent directory's contents (read-only context),
  with the entry you are currently inside marked `*`.
- **Right pane** (top rows) = the current directory, with the active `>`
  highlight.
- **Details line** between the list and the preview shows the selected
  entry's ProDOS type (TXT / SYS / DIR / BIN / …) and logical size in bytes
  (folders show `<FOLDER>`), parsed from the directory entry (`file_type` @
  `$10`, `EOF` 3-byte size @ `$15`).
- **Preview pane** (bottom rows) = a scrollable preview of the highlighted
  **TXT** file's head, with a status-bar footer; see *Text preview* below.
- **Line numbers:** the preview body has a left **gutter** with each source
  line's number (blank on a wrapped continuation row), so the footer's
  `LINE first-last/total` matches the numbers on screen. The gutter is
  **sized to the file** (digit count + 1 trailing space, like the editor's
  `editor_gutter_width`), so a short file's numbers hug the left edge instead
  of sitting in a fixed 4-digit field.
- **Position indicators** (every pane): the column header carries `PARENT n`
  (entry count) and `CURRENT sel/total`; the preview footer carries `LINE
  first-last/total` (source-line numbers). All glyphs stay in `$20..$5F` so
  they render on the pre-IIe character ROM.

### Listing + navigation

- **List:** GET_PREFIX → MLI OPEN the directory as a file → READ its 512 B
  blocks → parse ProDOS directory entries (header gives
  `entry_length`/`entries_per_block`; each entry's first byte = storage
  type nibble + name-length nibble, then the 15-char name, then file_type
  and the 3-byte EOF size). Show **every** named entry - files of any type
  and subdirectories (storage type `$0D`) - so the browser can manage
  anything on the disk; the details line's type column (TXT/SYS/DIR/…)
  distinguishes them. The current pane reads the current prefix; the parent
  pane reads the prefix's parent path (absolute, so no prefix change is
  needed to populate it). Page if a pane overflows.
- **Samples folder:** every program disk **and** the data disk ship the demo
  programs in a `SAMPLES/` subdirectory (AppleCommander 13.0's `-p`
  auto-creates the directory from the path). So a fresh boot's right pane
  shows `SAMPLES/`; entering it puts the root on the left and the programs on
  the right. (No `STARTUP.SWIFT` at the root - the auto-run path was dropped,
  see the ROADMAP section A.)
- **Move highlight:** `I` (up) / `M` (down) - letters, since the II+ has no
  up/down arrow keys - plus the //e Up/Down arrows (`$0B`/`$0A`) as modern
  aliases. Moving the highlight resets the preview dwell.
- **Enter / exit directory:** `→` / `.` on a subdirectory row does SET_PREFIX
  into it (Return works too); `←` / `,` does SET_PREFIX to the parent. So on
  the //e the four arrows read naturally - Up/Down select, Left/Right go
  out/in - and the unshifted `,` / `.` give the II+ the same in/out without
  arrows. The list re-reads at the new prefix. **Going up places the highlight
  on the directory you just left** (the old prefix's last component, looked up
  in the now-current pane) rather than resetting to the top, so stepping in
  and back out keeps your place. (Shipped disks put the demos in `SAMPLES/`
  off the volume root; navigation lets a user reach programs on other paths /
  volumes, and the **create-directory** action below makes subdirectories
  on-target.)
- **Scroll preview:** `J` (up) / `K` (down) page the bottom preview pane -
  home-row keys, since a finger reading the preview is already there.
- **Back to menu:** `Esc` or `Q`.

### Actions on the highlighted entry

Single-key actions; the action set is shown along the bottom two rows.

| Key              | Action  | MLI call         | Notes                                                       |
|------------------|---------|------------------|-------------------------------------------------------------|
| `→`/`.`/`Return` | Open    | (chain editor / launch_sys) | folder → enter; `.swift` → editor; `.SYSTEM` → launch |
| `X`              | Execute | (stage + chain)  | `.swift` files only; runs this disk's sole interpreter       |
| `E`              | Edit    | (chain editor)   | `.swift` files only - opens it in the in-process editor      |
| `F`              | New file| (chain editor)   | scratch buffer; Ctrl-S / Ctrl-R prompt SAVE AS              |
| `R`              | Rename  | `RENAME` ($C2)   | file or dir; line-editor for the new name                   |
| `D`              | Delete  | `DESTROY` ($C1)  | file or dir; confirm prompt; dir must be empty              |
| `N`              | Mkdir   | `CREATE` ($C0)   | storage type `$0D`; line-editor for the name                |

- **Execute** (`X`) stages the chosen file (the existing `$0C00` path) and
  chains this disk's sole interpreter. (The old `[L]`/`[S]`/`[A]` lite-vs-extras
  keys were removed with the 5-disk split - one interpreter per disk.)
- **Open/Edit** chains the in-process editor (Phase 14, merged into the
  launcher); on Ctrl-R it stages the saved file and runs it.
- **Rename / Mkdir** read a name with a small on-screen line editor. Typed
  names bypass the //+ auto-lowercase (ProDOS names are uppercase).
- **Delete** confirms first (`Delete NAME? Y/N`); a non-empty directory
  returns ProDOS error `$4E`/`$4A`-class → show a clean "directory not
  empty" message rather than a raw code.

### Text preview (bottom pane)

When the highlight rests on a **TXT** file (`$04` - how `.swift` samples are
stored) for ~1.5 s, the launcher previews its head in a permanent bottom
split so you can read a program before running it.

- **Layout.** The file list shrinks to the top rows (`PAGE_ROWS = 9`); the
  details line (row 11), a numbered preview body (rows 12–20), the
  `LINE …` status footer (row 21), then the two-line legend fill the bottom.
  The body is **scrollable**: `J` pages up, `K` pages down (home-row keys -
  the arrows / `,` `.` are used for directory in/out). A `J`/`K` scroll
  **repaints only the preview pane**, not the whole screen - the list, header,
  and legend above are unchanged by a scroll, so the slow `a_home` + full
  redraw is skipped (the body + footer are blanked in place first, since the
  render walk draws each row's content without padding it). Non-TXT entries
  (folders / SYS / BIN) show `(NO PREVIEW)`.
- **Dwell, not live.** The body is read + rendered only after the selection
  has been stable ~1.5 s (`DWELL_TICKS`, an idle `a_kbd` poll modeled on the
  rename/mkdir line editor), so fast `I`/`M` navigation never hits the disk.
- **No new buffer / no new MLI verb.** During browsing the `$0C00..$13FF`
  staged-source region is free (it is only written by `stage_file` at run
  time, and re-read fresh on chain), so the preview reuses the existing
  run-staging read - OPEN + `a_mli_read_startup` reads up to 2 KB of the file
  into `$0C00`, then CLOSE - but drops **no** `LASTRUN` note. (`$0C00`+2 KB =
  `$1400`, exactly where `CUR_TAB` begins, so a full read never overruns the
  directory tables.)
- **Per-machine rendering (mirrors `src/platform/apple2/screen.c` `emit()`).**
  Source files are canonical lowercase ASCII (design doc 003); the launcher
  is built once per disk, so the existing `#ifdef LITE_IIE` - **not** a
  runtime machine probe - picks the rendering:
  - II+ disk (`#ifndef LITE_IIE`): lowercase `a`–`z` → normal uppercase,
    capitals `A`–`Z` → inverse-video uppercase (design 003 rev 4/5);
    `{` `}` `|` → their input-method digraphs `<%` `%>` `??!`;
    other printable `$20..$5F` as-is; no-glyph / control bytes → space.
  - //e disk (`#ifdef LITE_IIE`): native - `SETALTCHAR` once, then lowercase
    renders from its `$E0..$FF` screen codes; control bytes → space.
  Each display line is positioned with `a_vtab` and clamped to the pane width
  (digraphs count as 2–3) so `COUT` never auto-wraps/scrolls into the list
  above; long source lines soft-wrap within the pane. The pane is **39 columns
  in 40-col mode**, widening to the **full 78 columns when 80-col is active**
  (`g_width80`, //e disk only - col 79 left clear to dodge the firmware
  auto-wrap), so a `.swift` file previews across the whole screen with far less
  soft-wrap. The wrap point is the pane width minus the dynamic gutter.

All of this lives in the **boot launcher** (MLI machinery already proven there
for OPEN/READ/CLOSE; adds CREATE/DESTROY/RENAME/SET_PREFIX/GET_PREFIX), so
the interpreter budget is untouched. Budget: the launcher's limit is now the
full ProDOS-SYS load ceiling, 40,704 B (`$2000`-`$BEFF`), the same window the
interpreters use - the launcher runs at `$2000` with all of low RAM free
before it chains, so the list+highlight UI and the four extra MLI verbs have
ample room.

## Compiler / Runner split for bigger programs → design 015

The staged-source path here caps a program at the lite source/bytecode
budget (2 KB / 1 KB) and runs only on lite. Running **bigger** programs is
the job of the Family B Compiler + Runner split, designed in
[015](015-bigger-programs-pascal-toolchain.md). An early sketch in this
doc proposed an *in-memory* bytecode handoff between the two binaries; the
shipped design instead hands off through a `.swb` **disk file** - the
in-RAM blob couldn't survive the runner's `$2000` chain READ, and a disk
file makes `.swb` a re-runnable artifact. The handoff still has to carry
the constant heap / string-pool alongside the bytecode (string literals
are heap-allocated at compile time and referenced by offset); see 015 for
the format and mechanism.

## Deferred within Phase 13 (must be handled before the phase closes)

- **`:quit` returns to the boot menu - DONE (all four binaries), 2026-06-03.**
  The interpreter clobbers ProDOS's LC RAM (`__LCADDR__ = $D000`), so its
  MLI body is gone - a full MLI re-chain is impossible, not just costly.
  The ProDOS 2.4 "enhanced QUIT with pathname" (pathname ptr in the QUIT
  param's reserved word) was tried and **crashes ProDOS 2.4.3** (Mariani +
  izapple2). So `platform_shutdown` (screen.c) **cold-reboots** with four
  inlined instructions: bank the ROM back into `$D000-$FFFF` (`bit $C082` -
  the same ROM-bank idiom `cout_char` already uses on this build; the
  interpreter's LC RAM hides the reset vector/monitor otherwise, and a naive
  `JMP $C600` died in the monitor at `$FF5A`), invalidate `$3F4`, then
  `JMP ($FFFC)` → autostart cold start → boots the disk → ProDOS
  auto-launches the first `*.SYSTEM` (SWIFTII.SYSTEM). Slot/version-
  independent; heavier than a QUIT (full reload). **Inlined, not a shared
  routine + `jsr`**, so the cost is the same handful of bytes on every binary
  (cc65 drops the dead epilogue after the terminal `jmp`) - which is what
  lets it fit **SWIFTSAT** (MAIN ~11 B headroom, can't afford a routine plus
  a call to it; now 1 B left, 40703/40704). The other three keep ample room
  (SWIFTIIP 74 / SWIFTIIE 2017 / SWIFTAUX 2259 B). All four binaries reboot
  to the menu on `:quit`, byte-identical sequence. **Deferred:**
  returning to the *file browser at the last
  directory + file* (vs the main menu) - a cold reboot clears RAM, so the
  explorer state would have to ride a disk file or a ProDOS-preserved page.

## Original (non-`+`) Apple ][ compatibility - DONE, 2026-06-04

The boot-from-disk path now also works on the **1977 Integer-BASIC Apple ][**
(with a 16K Language Card for its 64 KB) - same II+ disk and binaries, no extra
build. ProDOS 2.4.3 (the J.B. Brooks "all Apple II" release we already ship)
boots there fine; the only blocker was the **toolchain**: cc65's apple2 `crt0`
copies its language-card segment into the LC at startup with the Applesoft
routine **BLTU2 (`$D39A`)**, which exists from the ][+ on but is unrelated code
on the original Integer-BASIC ROM - so a program derailed right after the ProDOS
banner (izapple2 panics; a real ][ / Mariani fall into Integer BASIC →
`*** RANGE ERR`, where 50688 = `$C600` > Integer BASIC's max 32767).

cc65 2.19 ships no `apple2-integer-basic-compat.o` and hardcodes the `$D39A`
address, so the launcher **and all four interpreters** instead link a custom
startup, **`src/platform/apple2/crt0_ibasic.s`** (wired via the `A2_CRT0`
Makefile var). It is byte-identical to the stock crt0 except the LC copy calls
cc65's own `__fastcall__ _memcpy` (already linked; no-ops a zero-length LC) in
place of `jsr $D39A`. Because `_memcpy` is already present, the call site is a
few bytes *smaller* than stock - so every binary fits, including the tight
SWIFTSAT (this slightly relaxes the `:quit`-era headroom figures above):
post-change MAIN headroom is SWIFTIIP 44 / SWIFTSAT 11 / SWIFTIIE 1987 /
SWIFTAUX 2269 B (budget 40,704). The copy runs once at startup, so there is no
runtime/REPL speed cost on any machine, and the LC load image (in MAIN, `<$C000`)
never overlaps its `$D000+` run area, so the low→high `_memcpy` is safe.

Booting: the original ][ has the non-autostart ROM, so it comes up at the
monitor `*` prompt rather than auto-booting - type `C600G` (or `PR#6` from
BASIC). Emulator config: `make run-iz-ii` (izapple2 `-model=2 -s0 language`);
user-verified boot+run on the original ][ and `run-iz-iip` regression, 2026-06-04.
See LESSONS 2026-06-04 for the cc65/BLTU2 detail.
