# demo/capture/

Records the SwiftII demo video straight from the emulator by replaying the
keystroke script in [`../VIDEO-DEMO.md`](../VIDEO-DEMO.md) — no manual typing, no
focused window, no flaky keyboard. The output is **raw emulator footage**
(per-act MP4s + a concatenated full clip); captions, the end card, and your
**real-hardware cut-ins** are layered on afterwards in a video editor.

## How it works

[`capture_demo.py`](capture_demo.py) drives izapple2's `headless` frontend — the
same deterministic stdin protocol the acceptance harness uses (`run <cycles>`,
`key`/`type`/`enter`, `text`, `png`). For each act it boots the right machine +
disk, then captures the framebuffer as a PNG per frame while it walks the beats.
ffmpeg encodes the frames to MP4.

**Disk timing is real.** izapple2 models Disk II seek/read timing, so a ProDOS
boot is ~150M cycles and every directory read / program load is ~10M cycles
(≈10 emulated seconds). The tool **fast-forwards** through that dead time
(advancing in big chunks without capturing, anchored on an on-screen token) so
the video cuts straight to each result instead of filming a spinning disk. Only
the actual beats — typing, output, graphics, the editor — are captured frame by
frame.

It does **not** burn captions into the frames — keep that in the edit so the text
stays editable and the footage stays clean for overlays.

**Typing looks human.** The REPL and editor lines are typed one keystroke at a
time via the emulated keyboard (`--type-cps`, default 8 chars/sec), so they build
up on screen the way a person types — not all at once. The keys go through the
real keyboard path (not a paste), so the II+ input method runs live: letters
auto-lowercase, and `??/` folds to `\` as the third key lands.

## Realtime footage vs. PNG frames

The capture stitches one PNG per frame rather than screen-recording a window —
**on purpose**, and it is already realtime-*paced*:

- **It plays at real Apple II speed.** `--speed 1.0` advances exactly one second
  of emulated time per second of video, and typing / output / the editor are
  captured frame by frame, so motion is smooth. For text and (static) graphics
  the PNG stream is indistinguishable from a screen recording; raise `--fps` for
  smoother movement. (Only the dead disk-seek time is skipped — see above.)
- **True screen-recording isn't an option here.** The only scriptable izapple2
  frontend is `headless`, which renders to PNG, not video. The SDL GUI
  (`izapple2sdl`) *can* be screen-recorded, but it has no input-scripting API, so
  it can't be driven by this walkthrough deterministically — you'd be injecting
  OS-level keystrokes into a focused window against wall-clock timing, and the
  emulator runs faster than realtime, which throws off the cycle-accurate
  stepping the anchors rely on. The deterministic PNG path is what makes the run
  reproducible.
- **No audio.** `headless` has no sound, so the speaker beeps (`xspeaker`,
  `tone()`) aren't in the capture. Overlay the real-hardware audio, or add the
  effect in the edit.

**Relationship to the storyboard.** This automates a *robust subset* of
[`../VIDEO-DEMO.md`](../VIDEO-DEMO.md). A program run from the browser hands off
to the interpreter and ends at its `>` prompt, so the tool returns via `:quit`
(a cold reboot the launcher restores from); to keep the run reliable it shows the
in-editor edit and the 40/80 toggle but does **not** drive the editor's own
`Ctrl-R` run. Act 1 runs two samples (graphics + sound) after browsing the
preview pane; tweak the act functions to add more.

## Prerequisites

- **Disks:** `make disks` (builds every program disk + the data disk into
  `build/disk/`). Build fresh disks so the //e 80-col preference and the file
  browser start from a known state.
- **`headless` binary:** `make acceptance-build`, or
  `go install github.com/ivanizag/izapple2/frontend/headless@latest`. Set
  `IZAPPLE2_HEADLESS=/path/to/headless` if it is not on `PATH` or in `$GOPATH/bin`.
- **ffmpeg** on `PATH` (encoding). Use `--no-encode` to keep only the frames.

## Usage

```sh
make disks                                  # once
python3 demo/capture/capture_demo.py        # all acts -> demo/capture/out/

python3 demo/capture/capture_demo.py --act 1     # just one act
python3 demo/capture/capture_demo.py --smoke     # boot+menu only (pipeline check)
python3 demo/capture/capture_demo.py --dry-run   # print the plan, run nothing
python3 demo/capture/capture_demo.py --fps 24 --type-cps 6   # smoother + slower typing
```

Or via the Makefile: `make demo-video` (all acts) / `make demo-video-smoke`.

Outputs land in `demo/capture/out/`: `act1.mp4`, `act2.mp4`, `act3.mp4`, and
`demo_full.mp4` (the three concatenated). `out/` is git-ignored.

| Flag | Meaning |
|------|---------|
| `--act 1\|2\|3\|all` | which act(s) to capture (default `all`) |
| `--fps N` | output frame rate (default 15; raise for smoother motion) |
| `--speed S` | emulated seconds shown per second of video (default 1.0 = real ][ speed) |
| `--type-cps C` | typing speed for REPL/editor lines, chars/sec (default 8) |
| `--keep-frames` | keep the PNG frames next to the MP4 |
| `--no-encode` | capture frames only (skip ffmpeg) |
| `--smoke` | boot Act 1's disk to the menu and encode ~3 s — validates the toolchain |
| `--dry-run` | print the act/disk plan and exit |

## Tuning

The acts are short functions (`act1`/`act2`/`act3`) written in a small
vocabulary — `fast_forward(token)`, `wait_for(token)`, `hold(seconds)`,
`press(code)`, `type_line(text)`, `select(name)`. To lengthen a graphics beat,
change its `hold(...)`; to swap a sample, change the `select(...)` name. Timing
constants (boot fast-forward budget, key codes) sit at the top of the file.

The beats are **best-effort**, anchored to on-screen text where it can be scraped
(menu, REPL banner, selector legend) and to real-time holds where it can't
(lo-res graphics). Sample navigation uses `select()`, which scrolls the
highlight until the named entry is selected, so it is robust to disk ordering.
Review `demo_full.mp4` and nudge the holds to taste.

## Caveats

- **80-column note (Act 3):** on a *fresh* //e disk the first boot asks
  `Use 80 columns? [Y]/[N]`; the script answers `Y` and keeps it. If a `SWIFT80`
  preference already exists the disk boots silently into 80 and the script skips
  the prompt automatically.
- **Recreate the disks fresh before each capture run.** The emulator writes back
  to the `.po` it boots (Act 1 saves `demo.swift`, the //e act saves the `SWIFT80`
  width preference), and `make disk-*` is a no-op when the image is "up to date",
  so a re-run reuses the *dirty* disk — Act 1's `F` then opens the stale
  `demo.swift` instead of a fresh scratch buffer and the typed program lands on
  top of accumulated junk (`COMPILE ERROR`). Force a clean image first:
  `rm build/disk/*.po && make disks` (or delete just the disks an act uses).
- This captures the *emulator*. Frame your **real ][+** footage separately and
  overlay it in the edit (see the shooting notes in `../VIDEO-DEMO.md`).
- Graphics colour: the headless renderer outputs flat palette colour (no NTSC
  composite blur), which is what you want for a crisp recording.
