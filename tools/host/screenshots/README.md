# tools/host/screenshots — authentic doc screenshots

Generates the SwiftII screenshots in [`docs/screenshots/`](../../../docs/screenshots/)
by driving a **real** emulated Apple II and snapshotting its framebuffer — the
same izapple2 `headless` engine the [acceptance harness](../acceptance/) uses
(embedded ROMs, a deterministic run-then-inspect stdin protocol). Every image is
a genuine Apple II screen, not a mock-up.

```sh
make acceptance-build                 # one-time: install the `headless` binary
make screenshots                      # the whole set -> docs/screenshots/
make screenshots SHOTS="repl-hero graphics"   # just these
make screenshots-list                 # list the shot names
```

`capture.py` imports the protocol client (`Headless`) and navigation helpers
(`boot_launcher`, `pick_volume`, …) straight from
[`run_acceptance.py`](../acceptance/run_acceptance.py), so the two stay in
lock-step. It boots a **throwaway copy** of each disk (the launcher persists the
//e 80-column choice and the LASTRUN resume to its boot disk, so a fresh copy
keeps every shot deterministic and never mutates `build/disk/`).

## The shots

| name | machine | what it shows |
|------|---------|---------------|
| `repl-hero` | II+ | the opening REPL one-liner (mono, uppercase) |
| `repl-iie` | //e | a richer REPL session in native lowercase |
| `launcher` | II+ | the boot launcher menu |
| `browser` | II+ | the file browser + a live `.swift` code preview |
| `editor-iip` | II+ | the full-screen editor, 40-column uppercase |
| `editor-iie` | //e | the editor, 40-column, native upper + lowercase |
| `editor-iie80` | //e | the editor, 80-column (Ctrl-W toggles width) |
| `compiler` | II+ + Saturn | the Family B compiler + runner executing a `.swb` |
| `graphics` | II+ + Saturn | the `xgraphics` sample's lo-res colour output *(opt-in)* |
| `snake` | II+ + Saturn | animated GIF of the `xsnake` light-cycle game *(opt-in)* |
| `grdemo` | II+ compiler + data disk | animated GIF of the `xgrdemo` five-scene lo-res showcase *(opt-in)* |

`graphics`, `snake`, and `grdemo` are **not** in the default `make screenshots`
set, so a bare run never clobbers the curated `graphics.png` Mariani capture and
never spends time on the slow animations.

Run them by name: `make screenshots SHOTS=graphics` (izapple2/NTSC version),
`make screenshots SHOTS=snake`, or `make screenshots SHOTS=grdemo`. `snake` and
`grdemo` need **ffmpeg** on `PATH` (they capture frame-by-frame and stitch the
PNGs into a GIF). `grdemo` boots the **II+ Family B compiler** disk with the
**data disk** in drive 2 and `[X]`-runs `XGRDEMO.SWIFT` off it — the oversize
showcase's source lives only on the data disk, streamed off drive 2 by the
compiler on drive 1.

`colorbars.png` is also a manual Mariani capture and is never created by
`make screenshots`. The Mariani capture recipe for both colour stills is in
[docs/screenshots/README.md](../../../docs/screenshots/README.md).

## Notes / gotchas (worth knowing if you extend it)

- **Step the browser one key at a time.** The `>` row cursor only tracks the
  real selection when each `M`/`I` gets a full redraw budget (`run(1500)`);
  pressing faster drops keys. Read the `>` marker (`_marked`), **not** the
  preview header (`_highlight_name`) — selecting a file kicks off a preview
  disk-read that lags the header by millions of cycles.
- **Folders descend with `.` (not RET).** RET *launches* a `.SYSTEM` file, which
  exits the browser. Entering a folder reads the directory off the floppy, which
  costs ~14M emulated cycles — wait on the `DIR:` path changing, not a redraw.
- **Act on the top file.** SAMPLES sorts alphabetically, so the first entry is a
  stable target reachable with no stepping at all.
- **izapple2 `headless` lo-res colour is NTSC-soft and unavoidable.** The `png`
  command emits NTSC colour (colour extracted from a mono signal, so soft); the
  only crisp option is `pngm` (monochrome). There is no crisp-RGB lo-res mode —
  that is why the colour shots are captured from Mariani by hand. (`headless`
  also has `gif`/`gifm` for animation — handy for `xsnake` gameplay.)
