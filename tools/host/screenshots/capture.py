#!/usr/bin/env python3
"""capture.py — generate authentic SwiftII screenshots for the docs.

Drives izapple2's `headless` frontend (the same engine the acceptance harness
uses: embedded ROMs, a deterministic run-then-inspect stdin protocol) to boot a
real Apple II, type into the REPL / launcher / file browser / editor, run a
program, and snapshot the framebuffer to PNG. Output lands in `docs/screenshots/` at the
repo root.

    make screenshots                 # the whole set
    python3 tools/host/screenshots/capture.py            # same
    python3 tools/host/screenshots/capture.py repl-hero graphics   # just these
    python3 tools/host/screenshots/capture.py --list

Needs the `headless` binary (`make acceptance-build`); set IZAPPLE2_HEADLESS if
it is not on PATH. It reuses the protocol client and navigation helpers from the
acceptance harness, so the two stay in lock-step.
"""

from __future__ import annotations

import os
import re
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools" / "host" / "acceptance"))

import run_acceptance as ra  # noqa: E402  (path set above)

DISK_DIR = ROOT / "build" / "disk"
OUT_DIR = ROOT / "docs" / "screenshots"


def _headless_bin() -> str:
    env = os.environ.get("IZAPPLE2_HEADLESS")
    if env and Path(env).exists():
        return env
    onpath = shutil.which("headless")
    if onpath:
        return onpath
    gp = Path(os.environ.get("GOPATH", Path.home() / "go")) / "bin" / "headless"
    if gp.exists():
        return str(gp)
    sys.exit("headless binary not found; run `make acceptance-build` or set "
             "IZAPPLE2_HEADLESS")


# --------------------------------------------------------------------------
# Emulator + navigation helpers (built on the acceptance harness primitives)
# --------------------------------------------------------------------------

WORK_DIR = ROOT / "build" / "screenshots"


def boot(flags: str, disk: str, data: str | None = None):
    """Launch a headless emulator on `disk` (a basename in build/disk) with the
    named izapple2 machine `flags` (a key into ra.FLAGS). The disk is mounted
    read-write, and the launcher persists state (the //e 80-column choice, the
    LASTRUN resume), so we boot a fresh throwaway copy each time — pristine state
    every shot, and the source image in build/disk is never mutated. Pass `data`
    (a build/disk basename) to also mount the data disk in drive 2 — needed for
    the data-disk-only Family B showcases (xbig/xgrdemo/xfuncs), whose oversize
    source is streamed off drive 2 by the compiler on drive 1."""
    src = DISK_DIR / disk
    if not src.exists():
        sys.exit(f"missing disk {src}; build it first (e.g. `make disks`)")
    WORK_DIR.mkdir(parents=True, exist_ok=True)
    work = WORK_DIR / disk
    shutil.copy2(src, work)
    diskii = f"diskii,disk1={work}"
    if data:
        dsrc = DISK_DIR / data
        if not dsrc.exists():
            sys.exit(f"missing data disk {dsrc}; build it first (`make disk-data`)")
        dwork = WORK_DIR / data
        shutil.copy2(dsrc, dwork)
        diskii += f",disk2={dwork}"
    args = [*ra.FLAGS[flags], "-s6", diskii]
    return ra.Headless(_headless_bin(), args, OUT_DIR)


def boot_launcher(d, sentinel: str, eighty: bool = False):
    """Boot to the launcher menu, waiting for `sentinel`. On a //e the launcher
    first asks 'Use 80 columns? [Y]/[N]' (the II+ has no 80-col card and boots
    straight to the menu); answer Y for an 80-column run, else N."""
    answered = False
    for _ in range(60):
        d.run(8000)
        s = d.text()
        up = s.upper()
        if not answered and "USE 80 COLUMN" in up:
            d.key(ord("Y") if eighty else ord("N"))
            answered = True
            continue
        if sentinel.upper() in up:
            return s, True
    raise RuntimeError(f"launcher never reached (waiting for {sentinel!r})")


def enter_repl(d):
    """Boot to the launcher and enter the REPL; return at the `> ` prompt."""
    s, _ = boot_launcher(d, "REPL")
    d.key(ord(ra.menu_key(s, "REPL") or "1"))
    ra.wait_for(d, ":HELP")


def open_browser(d, eighty: bool = False):
    """Boot to the launcher, open the file selector, open a program volume; land
    sitting in the browser root. The //e lists its empty /RAM disk first, so we
    pick the first volume that has files (not /RAM) rather than the default."""
    s, _ = boot_launcher(d, "FILE SELECTOR", eighty)
    d.key(ord(ra.menu_key(s, "FILE") or "2"))
    ra.wait_for(d, "DISKS")
    target = None
    for _ in range(8):
        d.run(700)
        names = [n for _, n in ra._vol_rows(d.text())]
        target = next((n for n in names if "RAM" not in n), None)
        if target:
            break
    if target and not ra.pick_volume(d, target):
        raise RuntimeError(f"could not open volume {target}")
    elif not target:
        d.enter()                   # single volume — just open the highlighted one
    ra.wait_for(d, "[E]DIT")


def _dir_path(d) -> str:
    m = re.search(r"DIR:\s*(\S+)", d.text(), re.IGNORECASE)
    return m.group(1) if m else ""


def _marked(d) -> str:
    """The browser entry the `>` cursor is on. Read this rather than the preview
    header (`_highlight_name`): selecting a file kicks off a preview disk-read, so
    the *header* lags by millions of cycles, but the `>` marker moves instantly."""
    for line in d.text().splitlines():
        m = re.search(r">\s*/?([A-Za-z0-9.]+)", ra._unbox(line))
        if m:
            return m.group(1).upper()
    return ""


def step_to(d, name: str, tries: int = 30) -> None:
    """Move the browser `>` cursor down onto `name`, one M at a time."""
    name = name.upper()
    for _ in range(tries):
        if _marked(d) == name:
            return
        d.key(ord("M"))
        d.run(1200)
    if _marked(d) != name:
        raise RuntimeError(f"browser entry {name!r} not reached")


def descend(d, folder: str):
    """Step onto `folder` and descend into it ('.' is the browser's IN key), then
    wait for the new listing to load. Entering a folder reads the directory off
    the floppy, which costs millions of emulated cycles — far more than an
    in-memory redraw — so the wait here is generous."""
    step_to(d, folder)
    d.key(ord("."))
    for _ in range(30):
        d.run(2000)
        if _dir_path(d).rstrip("/").endswith(folder):
            break
    else:
        raise RuntimeError(f"folder {folder} did not open")
    for _ in range(8):              # wait for the new pane's cursor to settle
        d.run(1000)
        h = _marked(d)
        if h.endswith(".SWIFT") or h.endswith(".SWB"):
            return
    raise RuntimeError(f"folder {folder} loaded but no file highlighted")


def open_editor_on_top(d, folder: str):
    """Open the file browser, descend into `folder`, and edit its first file —
    SAMPLES sorts alphabetically so the top entry (ARRAYS.SWIFT) is a stable,
    code-rich target that needs no in-folder stepping (which the per-file preview
    disk-read makes slow and racy)."""
    open_browser(d)
    descend(d, folder)
    d.key(ord("E"))                        # [E]dit the highlighted (top) file
    ra.wait_for(d, "^S SAVE")
    d.run(2500)


def snap(d, name: str):
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    d.png(OUT_DIR / f"{name}.png")


def grab_frame(d, dest: Path):
    """Snapshot one PNG to `dest` (png writes snapshot.png in the CWD = OUT_DIR)."""
    d.cmd("png")
    src = OUT_DIR / "snapshot.png"
    if src.exists():
        src.replace(dest)


def assemble_gif(frames_dir: Path, out: Path, fps: int = 12, width: int = 564):
    """Stitch frame_%03d.png into an animated GIF with ffmpeg, nearest-neighbour
    scaled (keeps the lo-res blocks crisp) via a generated palette for clean
    colour. izapple2's own `gif` command is fixed at ~1s, so we record frames by
    hand and assemble here instead.

    Two ffmpeg 8.1.x bugs in the palette filters are worked around here:

    * `palettegen` fed a multi-frame stream collapses to ~2-3 colours no matter
      the input (so a mostly-black demo with a few vivid scenes came out
      grayscale). It works fine on a *single* image, so we build the palette from
      one montage image — a handful of frames spanning the run, hstacked into one
      picture via separate `-i` inputs — instead of from the frame stream.
    * `paletteuse` aborts ("Internal bug, should not have happened") while
      flushing the final input frame, leaving a valid-but-short GIF and a non-zero
      exit. We cap the output one frame short with `-frames:v` so the encode
      finishes before that buggy flush; the one dropped frame is the last sampled
      scene, invisible in a multi-second loop.

    On a fixed ffmpeg both work-arounds are harmless (a montage palette is still a
    fine global palette; the cap just drops one trailing frame)."""
    import subprocess
    frames = sorted(frames_dir.glob("frame_*.png"))
    n = len(frames)
    pat = str(frames_dir / "frame_%03d.png")
    palette = frames_dir / "palette.png"
    scale = f"scale={width}:-1:flags=neighbor"
    # Pick up to 8 frames spread across the run to source the palette from. A
    # lo-res Apple II screen is a fixed 16-colour palette, so a few frames from
    # different scenes already span the whole gamut.
    picks = frames if n <= 8 else [frames[i * (n - 1) // 7] for i in range(8)]
    montage_in = []
    for p in picks:
        montage_in += ["-i", str(p)]
    hstack = f"hstack=inputs={len(picks)}," if len(picks) > 1 else ""
    subprocess.run(["ffmpeg", "-y", "-loglevel", "error", *montage_in,
                    "-filter_complex", f"{hstack}palettegen", str(palette)],
                   check=True)
    cap = ["-frames:v", str(n - 1)] if n > 1 else []
    subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-framerate", str(fps),
                    "-i", pat, "-i", str(palette),
                    "-lavfi", f"{scale}[x];[x][1:v]paletteuse", *cap, str(out)],
                   check=True)


# --------------------------------------------------------------------------
# Shots — each boots a machine, drives it to an interesting screen, snapshots.
# --------------------------------------------------------------------------

def shot_repl_hero(name):
    """The hero REPL shot on a II+ (mono, uppercase): the opening code block."""
    d = boot("iip", "swiftii-iip-lite-repl.po")
    try:
        enter_repl(d)
        for line in ('let answer = 21 * 2',
                     'print("the answer is \\(answer)")'):
            d.repl_line(line)
        d.run(2000)
        snap(d, name)
    finally:
        d.close()


def shot_repl_iie(name):
    """A richer REPL session on a //e — native lowercase, so it reads as Swift."""
    d = boot("iie", "swiftii-iie-lite-repl.po")
    try:
        s, _ = boot_launcher(d, "REPL")
        d.key(ord(ra.menu_key(s, "REPL") or "1"))
        ra.wait_for(d, ":HELP")
        for line in ('let name = "Woz"',
                     'print("Hello, \\(name)!")',
                     'var xs = [2, 7, 1]',
                     'xs.append(9)',
                     'print(xs.count)',
                     'let maybe: Int? = 5',
                     'print(maybe ?? 0)'):
            d.repl_line(line)
        d.run(2000)
        snap(d, name)
    finally:
        d.close()


def shot_launcher(name):
    """The boot launcher menu on a II+."""
    d = boot("iip", "swiftii-iip-lite-repl.po")
    try:
        boot_launcher(d, "ABOUT")
        d.run(1500)
        snap(d, name)
    finally:
        d.close()


def shot_browser(name):
    """The file browser's preview pane showing a .swift's source off the disk."""
    d = boot("iip", "swiftii-iip-lite-repl.po")
    try:
        open_browser(d)
        descend(d, "SAMPLES")              # highlight lands on ARRAYS.SWIFT (top)
        ra.wait_for(d, "LINE 1")           # preview pane reads the file off disk
        d.run(1500)
        snap(d, name)
    finally:
        d.close()


def shot_editor_iip(name):
    """The full-screen editor, II+ 40-column (uppercase-only display)."""
    d = boot("iip", "swiftii-iip-lite-repl.po")
    try:
        open_editor_on_top(d, "SAMPLES")
        snap(d, name)
    finally:
        d.close()


def shot_editor_iie(name):
    """The full-screen editor, //e 40-column (native upper + lowercase)."""
    d = boot("iie", "swiftii-iie-lite-repl.po")
    try:
        open_editor_on_top(d, "SAMPLES")
        snap(d, name)
    finally:
        d.close()


def shot_editor_iie80(name):
    """The full-screen editor, //e 80-column (Ctrl-W toggles width in-editor)."""
    d = boot("iie", "swiftii-iie-lite-repl.po")
    try:
        open_editor_on_top(d, "SAMPLES")
        d.key(23); d.run(3000)             # Ctrl-W -> 80-column editor
        snap(d, name)
    finally:
        d.close()


# Shots kept OUT of the default `make screenshots` run:
#   graphics — the shipped graphics.png is a crisper Mariani capture (izapple2's
#     headless `png` only emits NTSC colour, soft by design); don't clobber it.
#   snake    — an animated GIF that runs the game live; slow + special-cased.
# Name either explicitly to (re)generate it (`make screenshots SHOTS=snake`).
MANUAL = {"graphics", "snake", "grdemo"}


def shot_graphics(name):
    """Low-res colour graphics (izapple2/NTSC): the xgraphics sample. The shipped
    graphics.png is a crisper Mariani capture; this is the on-demand fallback."""
    d = boot("sat", "swiftii-iip-sat-repl.po")
    try:
        open_browser(d)
        descend(d, "XSAMPLES")
        step_to(d, "XGRAPHICS.SWIFT")
        d.key(ord("X"))                   # [X]ec the highlighted .swift
        ra.wait_for(d, "PRESS RETURN", chunk=8000, tries=30)
        d.run(1500)
        snap(d, name)
    finally:
        d.close()


def shot_snake(name):
    """An animated GIF of the xsnake light-cycle game. xsnake ships on the Saturn
    *compiler* disk; [X] compiles + runs it, then it waits for a 1-9 speed digit.
    We then drive it one game-frame at a time — run a small cycle slice, snapshot
    a PNG, inject a turn key at the planned frames (sent as plain ASCII; the
    emulator adds the high bit) — and stitch the frames into a GIF. The path is a
    big open rectangle that stays in bounds for the whole clip, so the snake
    never crashes on camera."""
    import shutil as _sh
    frames = WORK_DIR / "snake_frames"
    if frames.exists():
        _sh.rmtree(frames)
    frames.mkdir(parents=True)
    PER_FRAME = 130                       # ~one game step per captured frame
    M, J, I = ord("M"), ord("J"), ord("I")
    # turn at frame -> key: right (default) -> down -> left -> up, ~open rectangle
    turns = {12: M, 24: J, 48: I}
    nframes = 60
    d = boot("sat", "swiftii-iip-sat-compiler.po")
    try:
        open_browser(d)
        descend(d, "XSAMPLES")
        step_to(d, "XSNAKE.SWIFT")
        d.key(ord("X"))
        ra.wait_for(d, "SET THE SPEED", chunk=8000, tries=50)
        d.key(ord("9"))                   # fastest -> a clear cell per captured frame
        d.run(500)
        for i in range(nframes):
            if i in turns:
                d.key(turns[i])
            d.run(PER_FRAME)
            grab_frame(d, frames / f"frame_{i:03d}.png")
        assemble_gif(frames, OUT_DIR / f"{name}.gif")
    finally:
        d.close()


def open_data_volume(d):
    """Boot to the launcher, open the file selector, and open the SWIFTII.DATA
    volume (drive 2). Lands sitting in the data disk's browser root. Used by the
    data-disk-only showcases that the compiler on drive 1 streams off drive 2."""
    s, _ = boot_launcher(d, "FILE SELECTOR")
    d.key(ord(ra.menu_key(s, "FILE") or "2"))
    ra.wait_for(d, "DISKS")
    if not ra.pick_volume(d, "SWIFTII.DATA"):
        raise RuntimeError("could not open SWIFTII.DATA volume")
    ra.wait_for(d, "[E]DIT")


def shot_grdemo(name):
    """An animated GIF of the xgrdemo lo-res GR showcase. xgrdemo's source is
    oversize, so it lives ONLY on the data disk (drive 2) and runs ONLY through
    the Family B compiler on drive 1: [X] streams + compiles the .swift, then the
    Runner paints five auto-advancing scenes (colour bars, concentric squares, a
    flying starfield, a bouncing ball, a rainbow flash). No keys needed — the
    scenes self-advance — so we wait for the compile to reach scene 1, sample the
    ~53 M-cycle run at a steady cadence (chosen so the whole demo lands in ~337
    frames), and play it back sped up. We do NOT dedup static holds: izapple2's
    NTSC colour decode advances its chroma phase every frame, so no two colour
    snapshots are ever byte-identical, and `pngm` on this build is colour too — so
    a pixel-exact dedup keeps everything. A steady linear sample is honest anyway:
    the auto-advance countdowns are part of the demo.

    The GIF is sped up: each frame is PER_FRAME=157 kcycles of emulated machine
    time, played back at fps=13. On a real ~1.0205 MHz Apple II those 157,000
    cycles take 157000/1020500 ~= 0.154 s, but the GIF shows them in 1/13 ~=
    0.077 s, so playback runs 157000*13/1020500 ~= 2.0x faster than real
    hardware. The README caption notes this. (Finer sampling = more frames: the
    ~53 M-cycle demo lands in ~337 frames / a ~26 s GIF, vs ~120 / ~9 s at the
    earlier 5.6x. To change the speedup, scale PER_FRAME: speedup grows with
    kcycles/frame at fixed fps.)"""
    import shutil as _sh
    frames = WORK_DIR / "grdemo_frames"
    if frames.exists():
        _sh.rmtree(frames)
    frames.mkdir(parents=True)
    PER_FRAME = 157                       # kcycles/frame: ~53 M run -> ~337 frames (2.0x real speed @ fps=13)
    MAX_SAMPLES = 400                     # safety cap (broken early at the end)
    # Any Family B compiler disk works (xgrdemo wraps each scene in a function, so
    # its bytecode flushes to the bank and fits every tier's compile window); we
    # use the plain II+ compiler disk. The Runner on this Family B disk has the GR
    # builtins, and lo-res is stock Apple II hardware, so it paints with no extras
    # card. xgrdemo's source is oversize, so it lives only on the data disk (drive
    # 2) and runs only via the compiler's [X] streaming it off drive 2.
    d = boot("iip", "swiftii-iip-compiler.po", data="swiftii-data.po")
    try:
        open_data_volume(d)
        descend(d, "XSAMPLES")
        step_to(d, "XGRDEMO.SWIFT")
        d.key(ord("X"))                   # compile (stream off drive 2) + run
        # Wait through the compile until the program is actually running. Every
        # scene name ("COLOUR BARS", "SQUARES", ...) also appears in the source-
        # preview comment still on screen right after [X], so those false-match
        # the browser. The pause() countdown's "next in N..." is runtime-only
        # (in no comment), so it's an unambiguous "the Runner is painting" marker.
        ra.wait_for(d, "NEXT IN", chunk=8000, tries=60)
        # Sample the run to frame_*.png. Stop once the finale drops back to text and
        # prints "checksum =" (the verdict); discard that last text frame so the
        # loop ends on the flash, not the post-run text screen.
        kept = 0
        for _ in range(MAX_SAMPLES):
            d.run(PER_FRAME)
            p = frames / f"frame_{kept:03d}.png"
            grab_frame(d, p)
            if p.exists():
                kept += 1
            up = d.text().upper()
            if "CHECKSUM" in up or "PRESS ANY" in up:
                if kept:                  # drop the text-screen frame
                    (frames / f"frame_{kept - 1:03d}.png").unlink(missing_ok=True)
                    kept -= 1
                break
        print(f"  grdemo: {kept} frames")
        assemble_gif(frames, OUT_DIR / f"{name}.gif", fps=13)
    finally:
        d.close()


def shot_compiler(name):
    """The Family B on-disk compiler + runner: [X] a .swift -> compile -> run."""
    d = boot("sat", "swiftii-iip-sat-compiler.po")
    try:
        open_browser(d)
        descend(d, "SAMPLES")
        step_to(d, "FUNCTIONS.SWIFT")     # compiler disks ship FUNCTIONS + GREET
        d.key(ord("X"))                   # compile (+ run) the highlighted .swift
        # Catch it once the Runner is producing output / waiting at the end.
        ra.wait_for(d, "PRESS ANY KEY", chunk=8000, tries=40)
        d.run(1500)
        snap(d, name)
    finally:
        d.close()


SHOTS = {
    "repl-hero":     shot_repl_hero,
    "repl-iie":      shot_repl_iie,
    "launcher":      shot_launcher,
    "browser":       shot_browser,
    "editor-iip":    shot_editor_iip,
    "editor-iie":    shot_editor_iie,
    "editor-iie80":  shot_editor_iie80,
    "graphics":      shot_graphics,
    "snake":         shot_snake,
    "grdemo":        shot_grdemo,
    "compiler":      shot_compiler,
}


def main(argv):
    if "--list" in argv:
        for k in SHOTS:
            print(k)
        return 0
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    # No names given -> the auto set (everything but the hand-curated Mariani
    # shots, so a bare `make screenshots` never clobbers them). Naming a manual
    # shot explicitly still runs it.
    wanted = [a for a in argv if not a.startswith("-")] or \
        [k for k in SHOTS if k not in MANUAL]
    rc = 0
    for shot in wanted:
        fn = SHOTS.get(shot)
        if not fn:
            print(f"unknown shot: {shot}", file=sys.stderr)
            rc = 1
            continue
        print(f"=== {shot} ===")
        # izapple2 `headless` occasionally loses the startup race (no first
        # prompt / launcher never reached); a fresh boot clears it, so retry.
        last = None
        for attempt in range(3):
            try:
                fn(shot)
                ext = "gif" if shot in ("snake", "grdemo") else "png"
                print(f"  wrote docs/screenshots/{shot}.{ext}")
                last = None
                break
            except Exception as e:                   # noqa: BLE001
                last = e
                print(f"  attempt {attempt + 1} failed: {e}", file=sys.stderr)
        if last is not None:
            print(f"  FAILED: {last}", file=sys.stderr)
            rc = 1
    return rc


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
