# progdisk/

Sources that ship on the SwiftII **program (binary) disks** — the seven
bootable disks (four REPL + three Family-B compiler) of the eight-disk set
built by `make disks`. This is the counterpart to
[`datadisk/`](../datadisk), which holds the sources **exclusive** to the
non-boot data disk (drive 2). Samples are split by **destination**: a file lives
in exactly one of the two trees — whatever ships on a program disk lives here,
not under `datadisk/`.

| Item | On-disk name | What |
|------|--------------|------|
| `readme-repl.txt`         | `README.TXT` | the **one** canonical on-disk Help for the REPL disks |
| `readme-compiler.txt`     | `README.TXT` | the **one** canonical on-disk Help for the Family-B compiler disks |
| `samples/`                | `SAMPLES/`   | regular demos — run on **any** system (incl. the lite REPL) |
| `xsamples/`               | `XSAMPLES/`  | the **`x`**-prefixed **extras-REPL** demos (graphics/speaker-click/games) — run on the extras REPL **or** any Family B Runner |
| `fbsamples/`              | `XSAMPLES/`  | the **`x`**-prefixed **Family-B-only** demos (`random`/`switch`/`for-in`) — reject on every REPL, so they ship on the **data disk only** |

## On-disk Help (`readme*.txt`)

The boot launcher's old Help menu screen moved out to a `README.TXT` file in each
program disk's root (the About screen points the user there) to reclaim launcher
code space. There is **one canonical (mixed-case) source per family** —
`readme-repl.txt` and `readme-compiler.txt` — so the help can't drift between
builds. `build_po.sh` derives each disk's `README.TXT` from it:

- **//e disks** ship the source as-is (the //e renders lowercase natively).
- **II+ disks** get an all-caps fold (`README_UPPER=1`) so the help reads as
  plain native text on a machine with no lowercase glyphs — exactly as the
  launcher's `cout_str` folds the **About** screen on the II+, keeping the two
  in sync.
- The `@YEAR@` placeholder is substituted from `src/common/version.h`
  (`README_YEAR`), so the copyright year matches the About screen.

`make check-readme` (part of `make ci`) verifies every built disk's `README.TXT`
is the correct fold of its canonical source. They are plain text, so the
editor/preview show them verbatim (not the `.swift` typing model).

## Where these samples ship

| Disk | Carries |
|------|---------|
| **lite** (`SWIFTIIP`/`SWIFTIIE`)   | `samples/` only (x-programs can't run on a lite REPL) |
| **extras** (`SWIFTSAT`/`SWIFTAUX`) | `samples/` **+** `xsamples/` (NOT `fbsamples/` — those reject on a REPL) |
| **Family B** compiler disks        | a minimal inline set: `greet` + `functions` + `xsnake` |
| **data disk** (drive 2)            | the canonical FULL set: `samples/` + `xsamples/` + `fbsamples/` + the oversize showcases |

`fbsamples/` is **data-disk-only**: its programs use `random(in:)` / `switch` /
`for-in`, which every Family A REPL rejects (the deliberate dialect fork), so
shipping them on an extras REPL disk would only hand the user examples that fail
to run. The oversize showcases (`xbig`, `xgrdemo`, `xfuncs`) are also **not** here — see
[`datadisk/xsamples/`](../datadisk/xsamples). The data disk image references
**all** the trees, so program-disk samples appear on it too without being
duplicated in `datadisk/`.

Run any sample on the host (no emulator):

```
make host
./build/host/swiftii_host progdisk/samples/fizzbuzz.swift
```

## A note on line width (40 columns)

These sources are meant to be opened in the on-device editor, which is
**40 columns** wide minus a line-number gutter. The gutter is sized to the
file's line count — `digits(line_count) + 1` columns — so the text area is
`40 - gutter` (37 for a typical ≤99-line file, 36 once a file passes 100
lines, 38 for a tiny one).

Author every line to fit that budget so it isn't truncated with a `>` marker
when opened in 40-col mode. Wrap long `//` comments, and keep trailing inline
comments short (or move them to their own line above the code). Genuinely-long
code statements are left as-is.

The **80-column demos** (`xwide`, `xvtab`) are authored for an 80-column
display, so they target the 80-col editor's budget instead (`80 - gutter`).
Their **comments** are wrapped to fit, and only the deliberately wide `print()`
output literals (e.g. the 1-80 ruler) run past it — shrinking those would push
the on-screen output under 80 columns.

Mind the 2 KB staging cap too: comment wrapping adds bytes, so a Family-A file
near the cap may need its prose trimmed to stay under it.

## Regular samples — `samples/`

Run on **any** REPL (lite or extras):

- `fizzbuzz.swift` — `while`, `if`/`else if`/`else`, `%`, `print`.
- `fib.swift` — iterative Fibonacci with `let`/`var` in a loop.
- `strings.swift` — `+` concatenation, `\(...)` interpolation, `String(_:)`,
  `.count` / `.isEmpty`.
- `arrays.swift` — array literals, `append`, subscript read/write,
  `.count` / `.isEmpty`, ranges over the indices.
- `optionals.swift` — `T?`, `if let`, `if let … else`, `??`.
- `functions.swift` — positional parameters (the Apple II binaries are
  positional-only), `_` no-label parameter, arithmetic (`square`, `power`),
  bounded recursion, `min` / `max`.
- `greet.swift` — interactive input: `readLine()`, `??`, an inline prompt with
  `print(terminator:)`.

## Extras-REPL samples — `xsamples/`

The **`x`**-prefixed demos (x for e**x**tras) that need graphics or
`peek`/`poke` builtins (`gr`/`plot`/`peek`/`poke`/`text80`...). They run on the **extras REPL**
(`SWIFTSAT`/`SWIFTAUX`) **or** any Family B Runner. **Omitted from the lite
disks** (no graphics or memory builtins there).

Graphics / speaker-click / games (extras REPL or any Family B Runner):

- `xgraphics.swift` — low-res GR mode: `gr`, `color`, `plot`, `hlin`, `vlin`,
  `scrn`, `text`.
- `xcolors.swift` — all 16 lo-res colours as vertical bands (`gr` / `color` /
  `vlin` over `x % 16`). The source of the `colorbars.png` gallery shot.
- `xspeaker.swift` — `peek` / `poke` and a speaker click.
- `xvtab.swift` — verifies `vtab()` in 80-column mode (//e aux + II+ Videx).
- `xsnake.swift` — a playable snake-style trail game (a Tron light-cycle: the
  trail is permanent, no food): steer with the `I`/`J`/`L`/`M` diamond (`Q`
  quits), `peek`/`poke` for live keys, `scrn()` for self-collision (the screen
  *is* the game state — no array, no heap in the loop). Plays clean on Mariani,
  izapple2, and real hardware.

## Family-B-only samples — `fbsamples/`

The **`x`**-prefixed demos that need the **Family-B dialect** (`random(in:)` /
`switch` / `for-in`). Run with `[X]` on a compiler disk; **every Family A REPL
rejects these** — the deliberate dialect fork — so they are **never** placed on
a REPL program disk. They ship on the **data disk only** (browse/run from drive
2); a single-floppy Family B user gets `xsnake` from the minimal inline set.

- `xdice.swift` — the three **Phase 16** big-language features: `random(in:)`
  (fixed-seed RNG dice), `switch` (the craps sums), `for-in` over an array (a
  histogram bar chart). Self-checking tests: `datadisk/tests/fbtests/`
  (`trandom`/`tswitch`/`tforarr`).
- `xwide.swift` — an **80-column** number-guessing game for a II+ **Videx
  Videoterm**: `text80()` switches the Videoterm to 80 columns so the full
  1-80 ruler fits on one row, then `text()` restores 40. Needs a Saturn 128K +
  a Videoterm; on a 40-col machine `text80()` is a no-op (the ruler wraps) and
  it still plays. Text-only by design (the Videoterm has no `vtab` from SwiftII,
  and a string-built bar would exhaust the bump heap).

## A note on games

The pieces for simple games are here: text I/O (`readLine()`, `print`), live
single-key input (`peek($C000)` + clear the strobe with `poke($C010, 0)`),
low-res graphics (`gr`/`color`/`plot`/`hlin`/`vlin`), reading the screen back
(`scrn`), and the speaker (`poke($C030, 0)`). **Randomness** depends on family:
a Family B compiler disk has `random(in:)` (see `xdice`); the Family A REPLs
don't, so seed a small integer LCG from the player's key-press timing. Live
input and graphics are extras-only (`x`-prefixed); `readLine()` works on the
lite REPL too.
