# tools/host/acceptance — automated emulator acceptance harness

Drives the SwiftII emulator acceptance matrix **unattended**: for every
hardware configuration it builds the right disk(s), boots izapple2 on that
machine, injects keystrokes, scrapes the screen, snapshots graphics, and
reports pass/fail across the whole set. It is the automated outer loop for the
manual playbook in
[`docs/testing/TESTING-emulators.md`](../../../docs/testing/TESTING-emulators.md)
and the on-target sweep designed in
[`docs/contributing/design/018-on-target-test-harness.md`](../../../docs/contributing/design/018-on-target-test-harness.md).

```sh
make acceptance-build                  # build the izapple2 `headless` binary (Go)
make acceptance                        # the whole matrix
make acceptance CONFIGS="iip sat"      # just these configs
make acceptance ARGS=--dry-run         # print the plan, launch nothing
make acceptance ARGS=--window CONFIGS=iip   # watch it in a GUI window
make acceptance ARGS=--show CONFIGS=iip     # ...or as live text in the terminal
make acceptance RELEASE=releases/v1.0.1     # run pre-built disks, skip the build
make acceptance-list                   # list the configs
```

By default the harness **builds** each config's disk(s) fresh from source.
`RELEASE=<dir>` (passed to the script as `--disk-dir <dir>`) instead runs against
a directory of **pre-built** images — typically a tagged `releases/v<version>/`,
so you can verify exactly what shipped — and skips the `make` build step.

Either way, every program **and** data disk is copied into
`build/acceptance/<config>/` and run from the copy, so write scenarios (editor
saves, file CRUD) only touch the copies — the source images, a release set
included, are **never modified**.

To **watch the run**, two options: a GUI window (`--window`) or live text in the
terminal (`--show`).

### `--window` — a GUI window you can also drive

Opens a browser window that mirrors izapple2's *rendered* screen — real Apple II
video, graphics and colour included — refreshing as the harness drives it. It
doubles as an **interactive controller**: it opens right away, and **nothing
runs until you press *Start tests***, so you choose what to run first.

Layout — three full-height columns:

- The **screen** sits on the left, as the focus.
- A **check log** is its own column beside the screen, streaming every check the
  running config makes — passes dimmed, **failures in red with their reason**
  (e.g. `errtest-ESYNTAX  MISSING COMPILE ERROR …`), grouped by config and
  tallied — so a red ✗ chip tells you *which* check failed and *why*, not just
  that the config failed. It clears when you press *Start*; **drag its right
  edge** to widen it (trading width with the screen).
- A **panel** on the right shows: the current **build** (config) and emulated
  **hardware**; the **elapsed time** (resets when you press Start, stops when the
  last test finishes); a **time-remaining** estimate for this config and for the
  whole run (rough estimates that **self-correct** as configs finish); the
  current **stage** (read off the screen); the **keys** being injected
  (`4 SPACE ^D …`); and a **clickable checklist of every config**.

Each config in the checklist is a chip showing a plain-language scenario label
(e.g. *compiler sweep*, *keyboard*) rather than the raw id, plus its state:

- a **run-order number** (`#1`, `#2`, …) if it's queued — the first to run reads
  `#1` from the outset;
- ☐ if unticked;
- a verdict once it has run: running (▶), passed (✓), failed (✗), stopped (⏹).

The running config's full description sits under the title, and a **legend**
below the checklist explains every scenario type.

Controls:

- **Tick / untick configs** — every config starts ticked; click a chip to untick
  (or re-tick) it, or use **Select all** / **Deselect all**. Ticking is pure
  selection — only **Start tests** runs anything. Unticking a config mid-run
  drops it from the rest of the queue.
- **Start tests** — runs the ticked configs in order.
- **Stop current test** — confirms, then halts just the running one (marked
  *stopped*); the next queued config begins.
- **Stop all tests** — confirms, then halts the running one and clears the rest
  of the queue. Press *Start* again to re-run the ticked set.

The localhost server starts immediately and prints its URL; the configs named on
the command line are the ones ticked at startup (default: all). The run stays
interactive — press **Ctrl-C** in the terminal when you're done, and the
pass/fail report prints whatever ran. The terminal stays clean otherwise.
Stdlib-only: a tiny localhost server streams the framebuffer PNG `headless`
emits; no extra Python packages.

### `--show` — live text in the terminal

Echoes the **text** screen into the terminal instead (boot → menu → each test's
output), redrawing in place on each change — handy over SSH or with no browser.
Graphics / 80-col show as their 40-col text page.

(For a fully native emulator window you can also drive *by hand*, use the
interactive `make run-iz-*` targets — those aren't automated.)

## Why izapple2's `headless` frontend

izapple2 ships **embedded ROMs** (nothing to source or version-match) and its
`headless` frontend exposes a deterministic stdin protocol:

| command | effect |
|---------|--------|
| `run <n>` | advance exactly `n`×1000 CPU cycles, then pause (the key to determinism — run, then inspect) |
| `key <0-127>` | queue one keystroke (Ctrl-D = `key 4`, space = `key 32`) |
| `type <s>` / `enter` | type a string / queue Return |
| `text` | dump the text screen as ANSI to stdout |
| `png` | snapshot the framebuffer (graphics / 80-col) to `./snapshot.png` |

Same machine-config flags as `izapple2sdl` (`-model`, `-s0 saturn`,
`-s6 diskii,disk1=…,disk2=…`), so the harness reuses the profile→flags map from
`emulator/run_izapple2.sh`. MAME was evaluated and rejected for this: it needs
version-locked romsets that are painful to source per-developer.

**The `headless` frontend is not a prebuilt izapple2 download — it is compiled
from source.** izapple2's releases ship only the SDL2/console executables; the
`headless` command lives at
[`github.com/ivanizag/izapple2/frontend/headless`](https://github.com/ivanizag/izapple2/tree/master/frontend/headless)
and `make acceptance-build` builds it via `go install …/frontend/headless@latest`.
It has no tagged release (a `v0.0.0-…` pseudo-version off `master`), so
**recompile** (`make acceptance-build` again) to track upstream.

## How it works

```
run_acceptance.py   the whole thing: the config matrix, the izapple2 `headless`
                    stdin client (class Headless), the scenarios, the screen
                    scraping, and the Family-B TESTLOG read-back via AppleCommander.
```

Output lands in `build/acceptance/<config>/`: `menu.txt` / `results.txt` screen
dumps, `*.png` snapshots, the per-run writable disk copies (the program `.po`
and `data.po` it actually boots — so write scenarios never touch the originals),
and (Family B) the extracted `TESTLOG`.

## What each config verifies

| Config | Machine | Scenario | Verdict source |
|--------|---------|----------|----------------|
| `iip` / `iie` | II+ / //e | RUN TESTS sweep (CORE) | screen: each test's `FAIL 0` tally |
| `sat` / `iie-aux` | + Saturn / //e aux | sweep (CORE + XTESTS) | screen |
| `compiler{,-sat,-iie}` | II+/Saturn/​//e | sweep (CORE+XTESTS+FBTESTS) | **TESTLOG** (per-test PASS/FAIL) |
| `samples{,-sat,-iie}` | II+/Saturn/​//e compiler | `[X]`-run the `XSAMPLES/` showcases | screen: `xbig`=6265, `xgrdemo`=1552, `xfuncs`=210 (paged) / rejected (flat) |
| `errtests` | II+ compiler | run each `TESTS/ERRTESTS/` demo ([X]) | screen: the on-target compile/runtime error banner + message |
| `errtests-repl` | II+ lite REPL | run the same demos on a REPL disk | screen: the *interpreter's* error display (separate path) |
| `sat-graphics` / `iie-aux-graphics` | Saturn / //e aux | draw GR from REPL | PNG snapshot |
| `videx` | II+ + Saturn + Videx | `text80()` | PNG snapshot |
| `keyboard` / `keyboard-iie` | II+ / //e | every UI mode's keyboard shortcuts | screen assertions |
| `digraphs` | II+ lite REPL | type every digraph + case marker in the editor | screen + **saved-file read-back** |

The **`keyboard`** configs walk the shortcuts in each UI mode per
[`TESTING-keyboard.md`](../../../docs/testing/TESTING-keyboard.md) — launcher menu
(I/M nav, A→About, D→Debug, digit select), the **Debug pager** (all three pages
via the arrows, Esc), the **volume picker** + **file browser** (I/M/J/K/Ctrl-T/V,
RET), the **editor** (open, type, `EDITED`/cursor status, Ctrl-A/E, ←/→ arrows,
Ctrl-O/Ctrl-L up/down, Ctrl-W `_` on II+, the **//e** editor's Ctrl-W 40↔80
width toggle — round-tripped through the `JSR $C300` firmware path that used to
corrupt ProDOS MLI, then checked that the post-toggle directory re-read still
lists a real entry (the `editor-80col-listing-intact` guard) — and
Ctrl-Q→discard), and the **REPL**
(`1+2`→`3`, `:help`/`:list`/`:mem`/`:reset`, backspace, Ctrl-W `_` on II+,
Ctrl-D exit; plus //e history recall + the `W` 80-col toggle). Those
modes read the keyboard inline in cc65 loops — "manual only by design" in the
matrix — so this is the automated coverage that doc calls its test of record.

The **`digraphs`** config is the II+ typing model's on-target test of record. The
//+ has no `{ } [ ] \ | _` keys and no lowercase, so source is typed with
C-style digraphs (`<% %> <: :> ??/ ??!`) and `'`/`''` case markers, kept
*literal* in the editor buffer and canonicalised to real bytes only at save (the
display re-expands the canonical byte on a pre-IIe screen). The `input.c` /
editor `screen.c` logic is host-unit-tested, but only a real II+ run exercises
the whole keyboard → gap buffer → video RAM → ProDOS-MLI save → reload chain. On
a II+ lite REPL disk the harness opens a fresh `.swift` buffer and checks, in
order:

1. **rendered after adding** — every digraph and case-marked letter shows on
   screen in its typed (literal digraph) form;
2. **rendered after a delete** — Ctrl-D (the editor's destructive backspace —
   `$08` is its *non-destructive* left arrow) removes the trailing `??!` while
   the earlier `??/` survives, then it's re-typed;
3. **saved correctly** — the file is saved to the data disk by absolute path
   (`/SWIFTII.DATA/DGTEST.SWIFT`, so the writable drive-2 copy gets it, not the
   read-only program disk) and read **back off the image** with AppleCommander:
   every digraph collapsed to its target glyph, the case markers produced real
   capitals (`ABC`, not `abc`), Ctrl-W's `_` is there, and **no** raw digraph
   source (`<%`, `??/`, …) leaked onto disk. This read-back is the ground-truth
   case check the screen scrape *can't* give — inverse-vs-normal video (the //+
   case indicator) is indistinguishable in a text dump;
4. **rendered after opening** — reload the saved file: the canonical bytes now
   drive the display, so `{ } |` re-expand to `<% %> ??!` while `[ ] \` show
   with their native glyph (the brace-digraph `<:` and the backslash-trigraph
   `??/` are *gone*, replaced by `[1]` and a literal `\`).

Screen dumps for each step land in `build/acceptance/digraphs/`
(`after-typing.txt`, `after-save.txt`, `after-reopen.txt`) alongside the
extracted `DGTEST.SWIFT`. II+ only — the //e disks pass the keyboard through
natively (no digraphs, no case markers), so there's nothing to verify there.

The launcher's **RUN TESTS** menu entry chains `TESTRUN.SYSTEM`, which runs the
on-disk self-checking suites one per ProDOS reboot. Family A (REPL) tiers end
each test at the `> ` prompt and the harness presses **Ctrl-D** to advance;
Family B (Runner) auto-advances and the harness reads the per-test verdict back
from `TESTLOG`. A clean test prints `PASS n FAIL 0`; a real failure is a `FAIL`
not followed by `0`. Graphics / 80-col can't be scraped as text, so those
capture PNGs for review (drop reference PNGs into `golden/` to diff by hand).

The **`errtests`** config covers the one tier the sweep can't: the
`TESTS/ERRTESTS/` demos *deliberately* fail (they show each error message on
target), so the **Run tests** sweep skips them. Instead, on a II+ compiler disk,
the harness:

1. opens the file browser and descends to the data disk's `TESTS/ERRTESTS/`;
2. presses `[X]` on each file in turn (the launcher's LASTRUN resume reopens the
   browser on the just-run file, so `M` steps to the next);
3. identifies each file by its **alphabetical browser order** (the disk order),
   the same anchor the REPL variant uses — *not* the screen's path echo, which
   garbles for some names (see the J/M note below);
4. asserts the expected on-target banner (`compile error` / `runtime error`) and,
   for the compile demos, a *fragment* of the message (`break outside`,
   `missing return`, …). The path echo is kept as a cross-check: both the
   **Runner** echo (`RUNNING: …`) and the **Compiler** echo (`COMPILING: …`)
   render `J`/`M` correctly (see the note below), so either can confirm the path.

> **Pre-IIe `J`/`M` echo (was a garble, now fixed).** On a II+, upper-case
> `A`–`Z` render as inverse video by feeding screen code `c-0x40` through cc65
> `cputc`; for `J` that code is `$0A` and for `M` it is `$0D`, which `cputc`
> swallows as LF/CR. So a `J`/`M` in an upper-case name *used to* act as a stray
> carriage return mid-word and wrap the rest of the line back over itself
> (`ENAMELEN` → `ELEN`). `WITH_INVERSE_JM` writes those two letters straight to
> video RAM instead, and is now on for **every** pre-IIe binary that echoes
> upper-case text — the Runner (program output + `RUNNING:`) **and** the II+/
> Saturn Compilers (`COMPILING:`/`WROTE:`). The //e Compiler renders correctly via
> `WITH_IIE` (full ASCII). So no echo garbles any more. Identity still comes from
> browser order anyway — it matches the REPL variant and survives the 40-column
> wrap that a long path can hit regardless of `J`/`M`.

This is the on-**target** display path the host's `error_paths_test.c` can't
reach; per-file screen dumps land in `build/acceptance/errtests/<NAME>.txt`.

Why only a fragment, not the exact string? The `line N:` number drifts with a
file's comments, and the 40-column hard wrap can't be cleanly rejoined for both
mid-word and word-boundary splits. The exact strings are already pinned on the
host by `error_paths_test.c`; on target we only confirm the right message
*reached the screen*. The two runtime demos check the banner alone — the Runner
prints only `runtime error`, with no per-trap text.

> Messages quote tokens with single quotes (e.g. `expected ')'`), not
> backticks: the pre-IIe character ROM has no backtick glyph, so `` `)` `` would
> scrape as `@)@`.

The **`errtests-repl`** config runs the very same demos on a *REPL* disk
(`[X]` → the interpreter, not the Compiler), covering the REPL's own error
display (`repl.c` `run_source`): `compile error: <msg>`, with **no** `line N:`
and **no** "press any key". The REPL drops to the `> ` prompt, so the harness
settles there and advances with **Ctrl-D** (exit REPL → launcher resumes the
browser) + `M`. The REPL screen doesn't echo the file path, so demos are
identified by their alphabetical browser order, with the message fragment as a
cross-check. The interactive REPL error strings are also host-tested by
`tests/repl/017_errors`; this is their on-target counterpart.

> Note: the sweep is one full reboot per test, so a sweep config takes a couple
> of minutes of (fast-mode) emulation. That's expected.
>
> Caveats: the **`videx`** config is only a boot/command smoke test —
> izapple2's bundled Videoterm ROM diverges from a real card, so the snapshot
> shows the main 40-col page, not the Videx 80-col output; real 80-col is a
> hardware check. The **//e** launchers prompt "Use 80 columns?" at boot; the
> harness answers **N** to keep the run 40-column and text-scrapeable.
>
> **//e *firmware* 80-col is not headless-renderable.** Unlike the Videx page,
> the //e firmware 80-col display interleaves its 80 columns across main + aux
> RAM, and izapple2's headless `png`/`text` capture only the main page (every
> other column, garbled) — so there is deliberately **no** //e 80-col PNG config.
> The //e 80-col REPL input caret fix (commit "repl///e: draw our own 80-col
> cursor block …", which replaced a stray ALTCHAR backtick) therefore stays a
> manual / real-hardware eyeball check. The *other* half of the //e 80-col story
> — the editor's Ctrl-W width toggle corrupting MLI (commit "launcher///e: bank
> ROM before JSR $C300 …") — **is** automated as text: the `keyboard-iie` config
> toggles the editor 40↔80↔40 and asserts the next directory re-read still lists a
> real entry (the corruption manifests back in scrapeable 40-col as a garbled /
> empty listing). See the `editor-80col-listing-intact` check.

## Setup

Prerequisites (one-time):

| Need | Why | Install |
|------|-----|---------|
| **Python 3.8+** | the harness *is* `run_acceptance.py`; `make acceptance` runs it. **Standard library only** — no `pip install`. | macOS: preinstalled (or `brew install python`); Debian/Ubuntu: `apt install python3` |
| **Go** | builds the izapple2 `headless` emulator binary. | macOS: `brew install go`; Linux: distro package or [go.dev/dl](https://go.dev/dl/) |
| **cc65** | builds the SwiftII disks the harness boots (it runs `make disk-*` per config). | macOS: `brew install cc65`; Linux: distro package or [cc65.github.io](https://cc65.github.io/) |
| **Java + AppleCommander** | disk image building and reading files back off the image (the Family-B `TESTLOG` and the `digraphs` config's saved file). | already required by `make disks` — see the top-level README |

Then build the emulator once:

```sh
make acceptance-build      # go install …/frontend/headless@latest
```

That drops a `headless` binary in `$(go env GOPATH)/bin`; the harness finds it
there, on `PATH`, or via `IZAPPLE2_HEADLESS=/path/to/headless`. Now `make
acceptance` (or any of the variants at the top of this file) runs unattended —
no Python packages to install, no virtualenv.

## Status

Verified end-to-end on this machine: the `iip` CORE sweep (10/10); the
`errtests` (Family B compiler) and `errtests-repl` (REPL) error-display configs
(9/9 demos each); and the `digraphs` II+ typing-model config (16/16 — typing,
delete, save read-back, reload). Exercised across those: the protocol client,
screen scrape, menu-key parsing, the volume-picker + file-browser navigation
(descend into `TESTS/ERRTESTS/`, the LASTRUN-resume step loop), the editor
type/Ctrl-D/save-as/reopen flow, the AppleCommander read-back (`TESTLOG` and the
saved `.swift`), and the disk build.

Also verified (fresh-built disk, 3 runs, deterministic): the `keyboard-iie`
editor Ctrl-W 40↔80 width-flip steps (`editor-width-to-80` + `editor-width-to-40`
+ `editor-80col-listing-intact`, added with the "bank ROM before JSR $C300" MLI
fix) — the full `keyboard-iie` walk passes 43/43, including those three. Note the
editor's Ctrl-W needs **re-sending until the width flips**: headless can drop the
first key injected right after a redraw, so the harness polls the scraped box
width (it widens ~40→~80) and re-sends rather than assuming one key = one toggle.

A couple of izapple2 / harness quirks the code already handles, worth knowing if
you extend it:

- The machine must be `pause`d once before the first `run` (a startup race), and
  the TESTRUN intro's "ANY KEY = BEGIN" wants a *printable* key — Return does not
  satisfy it (use space).
- `headless`'s `text` dump wraps the 40-column screen in a `#`-border box, so
  column-anchored matches strip it (`_unbox`), and a message word-wrapped across
  two rows is rejoined before matching (`_flat`).
- The file-browser highlight in the `text` dump can lag the emulator's actual
  selection by a redraw, so **both** errtests configs identify the file that ran
  by alphabetical browser order, not by scraping the highlight (nor the path
  echo, which garbles for `J`/`M` names — see the J/M note above).
- The `type <s>` verb **strips a chunk's trailing space**, so a typed `x = ` lands
  as `x =`; the `digraphs` config checks the saved file by space-agnostic token,
  not exact spacing. And inside the editor, `key 4` (Ctrl-D) is the *destructive*
  backspace — `key 8` is the *non-destructive* left arrow (the cursor block over a
  char can even make a scrape look like the char was deleted when it wasn't).
