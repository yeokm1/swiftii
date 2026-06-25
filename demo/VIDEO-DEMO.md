# SwiftII — video demo script (emulator walkthrough)

A storyboard for a short demo **recorded from the [izapple2] emulator** via a
scripted keystroke walkthrough. The emulator gives a reliable keyboard, so every
beat is deterministic — no dead/sticky keys, no retyping on a 1 MHz machine.

> **Why the emulator drives the recording.** The filming Apple ][+ has several
> dead/sticky keys (`1`, `*`, `a`, `c`, `g`, `m`, a flaky spacebar), which makes
> a live typed walkthrough fragile. So the **primary capture is the emulator**,
> scripted below. **Real-hardware footage is overlaid separately** — short cut-ins
> of the genuine ][+ running the same samples — wherever it sells "this is real
> iron." This script only covers the emulator capture; mark the beats you want a
> hardware cut-in over and shoot those independently.

**Target length:** ~105 s. 720p; the emulator window at a clean integer scale.

[izapple2]: https://github.com/ivanizag/izapple2

---

## What's covered (the required feature set)

- **REPL** — type a line, get an answer (II+ Saturn).
- **File selector** — browse the disk, the live **preview pane** renders real
  Swift off the disk with zero typing, `X` runs a program.
- **Disk samples** — `xsnake` (an interactive lo-res game), plus `xgraphics` /
  `xspeaker` / `xvtab` shown through the preview pane.
- **Compiler + runner (bigger than RAM)** — `xgrdemo` (lo-res graphics too big
  for RAM) streamed off disk, compiled to bytecode, then run by the separate
  runner → five auto-advancing scenes and a `checksum`.
- **Editor — write a program from scratch** — a whole program typed live with the
  digraphs, saved, and run; on a //e disk toggle the editor 40↔80 with `Ctrl-W`.

### Two machine profiles (please confirm)

The spine runs on the **II+ Saturn** profile so it lines up with the real-][+
overlay footage. **One feature forces a second machine:** the **80-column
editor is a //e-disk-only path** — the II+ launcher/editor has no 80-col mode
(the II+ Videx 80-col support is for *programs* such as `xwide`, not the
editor). So Act 3's editor 40↔80 toggle runs on a **//e** profile. The hard cut
to a //e is honest for an emulator capture; if you'd rather stay all-II+, the
fallback is to drop the 80-col *editor* beat and instead show an 80-col
*program* under the **Videx** profile (`run-iz-videx-2disk`, run `xwide`).

---

## Setup (once, off-camera)

```sh
make disks          # builds all program disks + the data disk into build/disk/
```

Each act launches one disk with the data disk in drive 2 (`*-2disk` targets
mount `swiftii-data.po` as drive 2). The emulator binary committed at the repo
root (`./izapple2sdl_mac_arm64`) is picked up automatically.

| Act | Launch command | Boots |
|-----|----------------|-------|
| 1 — REPL + selector + samples + 40-col editor | `make run-iz-sat-2disk` | `SWIFTSAT` (II+ Saturn 128K) + data disk |
| 2 — compiler + runner / `xgrdemo` | `make run-iz-compiler-sat-2disk` | Family-B compiler (II+ Saturn) + data disk |
| 3 — editor 40↔80 columns | `make run-iz-iienh-2disk` | `SWIFTAUX` (//e, 80-col aux) + data disk |

**Emulator capture tips**

- **Screen mode:** press **F6** in the window to cycle to **"Plain"** — flat,
  pure-palette colour. The default NTSC composite renderer blurs lo-res colour
  into fringey bands; "Plain" gives a crisp recording of `xsnake`/`xgrdemo`.
- **F1** toggles the on-screen key help — keep it **off** while recording.
- Record the emulator window only (not the whole desktop); pause between acts
  and cut the disk-swap/reboot dead air out in the edit.

---

## Act 1 — the language, off the disk · II+ Saturn (≈0–80 s)

`make run-iz-sat-2disk`. Open on the REPL to prove it's a real language, then
switch to the file selector and let the **preview pane** show real Swift with no
typing; `X` runs the highlighted program.

| # | ~secs | On screen | You do (keys) | Caption overlay |
|---|------:|-----------|----------------|-----------------|
| 1 | 0–5 | Boot to the launcher menu; `SwiftII ][+ Saturn` masthead | (autoboots) | **Swift. On a 1979 Apple II.** |
| 2 | 5–9 | Pick the REPL **with the cursor** (not the number key) — `Return` on the lit option | **Return** (REPL is already highlighted) | *An interactive prompt.* |
| 3 | 9–18 | Type one sum, get the answer | `40+2` ⏎ *(auto-prints `42`)* | *A real language — type a line, get an answer.* |
| 4 | 18–26 | A string + interpolation | `let n = 7` ⏎ then `print("n=??/(n)")` ⏎ *(prints `n=7`)* | *Strings, interpolation — the real thing.* |
| 5 | 26–32 | Back to the launcher → cursor down to **File selector** → `Return` | `:quit` ⏎ → **↓** to File selector → **Return** | *…now off the disk.* |
| 6 | 32–40 | Enter `XSAMPLES`; the **preview pane** renders real Swift as you scroll | **↓** to `XSAMPLES` → **Return** (enter) → **↑/↓** to preview | *…compiled to bytecode, straight off the disk.* |
| 7 | 40–48 | Run **xsnake**: an interactive lo-res game — a speed digit starts it, the trail plays out and `crashed! trail length N` *(F6 = "Plain")* | **↓** to `XSNAKE` → `X` *(pick `[S]` Saturn if prompted)* → `5` to start → `:quit` ⏎ | *A game — graphics + input from Swift.* |
| 8 | 48–53 | Scroll the other samples — `XGRAPHICS`, `XSPEAKER`, `XVTAB` — reading each preview | **↓/↑** over the entries | *Real Swift, straight off the disk.* |

> Unsteered, `xsnake` heads right into the wall and ends on its own; steer with
> `I`/`J`/`K`/`M` for a longer trail. It ends at the `>` prompt, so `:quit` ⏎
> returns to the browser. `xgraphics` (lo-res colour fill) is an equally good
> run here, and both are ideal **real-hardware cut-ins** for the genuine ][+.

### 40-column editor — write a program from scratch (≈53–80 s, still on II+ Saturn)

The hero beat: a **whole program typed live**, digraphs and all, then run.

| # | ~secs | On screen | You do (keys) | Caption overlay |
|---|------:|-----------|----------------|-----------------|
| 9 | 53–56 | New empty file in the editor; `[DGR]` (digraph) mode in the title | `F` (new file) | *Write code on the machine.* |
| 10 | 56–73 | Type the program below — the **digraphs** stand in for the keys a 1978 board lacks | type the 6 lines *(see keying)* | *`<: :>` `<% %>` `??/` — it fits a 1978 keyboard.* |
| 11 | 73–76 | Save it | `Ctrl-S` → `demo.swift` ⏎ | *Save to disk.* |
| 12 | 76–80 | Run it → `total = 14` | `Ctrl-R` (save + run) | *…and run it. `total = 14`.* |

The typed program (canonical on the left, **what you key** on the right):

```
let nums = [3, 1, 4, 1, 5]        let nums = <:3, 1, 4, 1, 5:>
var total = 0                     var total = 0
for i in 0...4 {                  for i in 0...4 <%
  total = total + nums[i]           total = total + nums<:i:>
}                                 %>
print("total = \(total)")         print("total = ??/(total)")
```

`<:`→`[`  `:>`→`]`  `<%`→`{`  `%>`→`}`  `??/`→`\`; letters auto-lowercase, so you
type them as written. The launcher renders the canonical form back, and the saved
`.swift` is real Swift. It uses only core features (array literal + subscript, a
range `for`, interpolation), so it runs on the REPL disk.

---

## Act 2 — compiler + runner, bigger than RAM · II+ Saturn (≈80–92 s)

The REPL fits a program in RAM; the **compiler/runner** doesn't have to. It
streams a `.swift` off disk through a 4 KB window, writes a `.swb` bytecode
file, and a separate runner executes it — so program size is bounded by the
**disk, not 64 KB of RAM**. `xgrdemo` is ~8.5 KB of source, *three times* too
large for the compiler's window (and too big for the REPL buffer and the
editor), yet it paints **lo-res graphics**: five auto-advancing scenes (colour
bars, concentric squares, a starfield, a bouncing ball, a rainbow flash), then
folds every scene's maths into one `checksum`.

`make run-iz-compiler-sat-2disk`. `xgrdemo.swift` is on the **data disk** (drive 2).

| # | ~secs | On screen | You do (keys) | Caption overlay |
|---|------:|-----------|----------------|-----------------|
| 13 | 80–83 | Compiler disk launcher → File selector → the **data disk** volume → `XSAMPLES` | **↓** to File selector → **Return** → pick `/SWIFTII.DATA` → **Return** → into `XSAMPLES` | *Same language. Bigger programs.* |
| 14 | 83–85 | Highlight `xgrdemo.swift`; the long preview scrolls | **↓** to `XGRDEMO` (preview) | *8.5 KB of source — too big for RAM.* |
| 15 | 85–92 | `X`: "compiling…" streams off disk, then the runner paints five lo-res scenes → `checksum` | `X` (compile + run); let the scenes auto-advance | *Streamed off disk, compiled, run — as graphics.* |

> `xbig.swift` (a pure-computation showcase ending `checksum = 6265`) lives in
> the same folder and runs the same way if you want a non-graphics finale
> instead. Both are bounded by the disk, not 64 KB of RAM.

---

## Act 3 — the editor at 80 columns · //e (≈92–104 s)

`make run-iz-iienh-2disk`. The **//e boots straight into 80 columns** — on the
first boot with an 80-col card it asks `Use 80 columns? [Y]/[N]`; answer `Y`
once and the choice is saved (a `SWIFT80` file on the boot volume), so every
later boot comes up in 80 columns silently. The whole launcher — menu, file
selector, preview pane, and the in-process **editor** — is 80-col from boot. So
here we open a wide sample at 80 columns, toggle *down* to 40 with `Ctrl-W` to
make the contrast, then **run** it — `xvtab` exists to prove `vtab()`/`htab()`
position correctly across all 80 columns, so its output is the act's payoff.

| # | ~secs | On screen | You do (keys) | Caption overlay |
|---|------:|-----------|----------------|-----------------|
| 16 | 92–96 | Boot the //e disk; `Use 80 columns? [Y]/[N]` → keep 80; launcher comes up wide | `Y` → any key to keep *(silent on later boots)* | *A //e — 80 columns from boot.* |
| 17 | 96–100 | File selector → open `XSAMPLES` → `xvtab.swift` in the editor at **80 cols**: full lines fit | **↓** to File selector → **Return** → **↓** to `XSAMPLES` → **Return** → **↓** to `XVTAB` → `E` (edit) | *80 columns — the whole line.* |
| 18 | 100–103 | `Ctrl-W` drops the editor to **40 columns**: the wide lines wrap; `Ctrl-W` back to 80 | `Ctrl-W` (80↔40 toggle), `Ctrl-W` again | *Toggle to 40 — wide code wraps.* |
| 19 | 103–105 | `Ctrl-Q` back to the browser | `Ctrl-Q` | *Same editor, two widths.* |
| 20 | 105–110 | `X` runs `xvtab`: bracketed labels land at the exact row/col they name, spread across the full 80 columns | `X` *(let it finish)* | *Run it — `vtab`/`htab` across 80 columns.* |

> If you'd rather show the first-boot prompt itself, build a fresh disk so no
> `SWIFT80` preference exists yet; otherwise it boots silently into 80. The file
> selector's own `W` key toggles the **browser** width too. `xvtab` runs on the
> //e aux interpreter directly (`X`); it ends at the `>` prompt and the grid
> stays up, so `:quit` ⏎ would return to the browser.

End card (hold 2 s): **SwiftII — github.com/yeokm1/swiftii** · *fits in 64 KB.*

---

## Reference — typing on a ][+ (the digraphs)

The emulator keyboard is reliable, so the typed REPL beats (3–4) and the
typed-from-scratch program (beats 9–12) work as written. The uppercase-only ][+ keyboard has no lowercase,
`{ } [ ] \ _`, or `|`, so an input layer maps what you type to canonical bytes.
Letters auto-lowercase, so `print(...)`, `plot(...)` are typed as written — but
**braces and the interpolation backslash are digraphs**, and showing them on
camera is part of the "fits a 1978 keyboard" story:

| Type this | To get | Where it shows up |
|-----------|--------|-------------------|
| `<%`  `%>` | `{`  `}` | a `for`/`while` loop body |
| `??/` | `\` | a `\(value)` string interpolation (beat 4) |
| `<:`  `:>` | `[`  `]` | array literals |
| `'` *before a letter* | one uppercase letter | `'String`, `'Int` |
| `Ctrl-W` | `_` | `square(_ x:)` etc. *(II+ editor; on //e `Ctrl-W` toggles width)* |

So beat 4's interpolation is keyed `print("n=??/(n)")`. A typed loop would be
`for i in 2...5 <% print(i+i) %>`. Inside a string, `'` between two letters is a
literal apostrophe. The launcher renders the canonical lowercase back, so what
the audience reads is real Swift. Full rules:
[design doc 003](../docs/contributing/design/003-apple2-input-method.md).

## Reference — launcher & editor keys

**Menu:** `1` REPL · `2` File selector · `3` Debug · `4` About.

**File selector:** **↑/↓** move · **Return / → / `.`** open (enter a folder,
open a `.swift` in the editor, or launch a `.system`) · `X` run a `.swift` ·
`E` edit · **← / `,`** up a directory / re-pick the drive (drive 2 = data disk)
· `W` toggle 40/80 col (//e only) · `Esc` / `Q` back to the menu.

**Editor:** arrows = cursor L/R · `Ctrl-O`/`Ctrl-L` up/down · `Ctrl-D`
backspace · `Ctrl-S` save · `Ctrl-R` save + run · `Ctrl-Q` back to the browser ·
`Ctrl-W` = `_` (II+ Swift mode) **or** 40↔80 width toggle (//e).

## Shooting notes

- **Pre-stage each act once off-camera** so the exact keys are muscle memory;
  the emulator removes the keyboard risk but not the pacing risk.
- Press **F6** for the "Plain" screen mode before the graphics beats
  (`xsnake`/`xgrdemo`); the default NTSC composite blurs lo-res colour.
- The disk swaps between acts are reboots — cut them out in the edit so each act
  starts on a live screen.
- Mark the beats you want a **real-hardware cut-in** over (the genuine ][+ is the
  whole hook); shoot those separately and overlay in the edit.
- Captions carry the story for muted autoplay; keep them ≤ 5 words.
