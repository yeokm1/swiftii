#!/usr/bin/env python3
"""capture_demo.py — record the SwiftII demo video from the emulator, unattended.

Drives izapple2's `headless` frontend (the same one the acceptance harness uses)
through the keystroke script in `demo/VIDEO-DEMO.md`, snapshots the framebuffer
as a stream of PNG frames, and encodes each act to an MP4 with ffmpeg. The output
is *raw emulator footage* — captions, the end card, and your real-hardware
cut-ins are added afterwards in a video editor (this tool deliberately does not
burn text into the frames).

Why headless, not screen-recording the SDL window: it is deterministic. `run N`
advances an exact number of cycles, `key`/`type`/`enter` inject input, and `png`
snapshots the screen — so the capture is reproducible and needs no focused window,
no manual typing, and no flaky-keyboard worries (the whole point of moving the
demo to the emulator).

    # build the disks once, then capture everything:
    make disks
    python3 demo/capture/capture_demo.py            # all three acts -> demo/capture/out/

    python3 demo/capture/capture_demo.py --act 1    # just Act 1
    python3 demo/capture/capture_demo.py --smoke     # boot+menu only, validates the pipeline
    python3 demo/capture/capture_demo.py --dry-run   # print the beat plan, run nothing
    python3 demo/capture/capture_demo.py --fps 20 --speed 1.0

Needs:
  - the izapple2 `headless` binary (`make acceptance-build`, or
    `go install github.com/ivanizag/izapple2/frontend/headless@latest`); set
    IZAPPLE2_HEADLESS=/path/to/headless if it is not on PATH or in $GOPATH/bin.
  - ffmpeg on PATH (for encoding; skip with --no-encode to keep only the frames).

Tuning: the timing constants are gathered at the top of this file and each act is
a short, readable function — adjust a hold or swap a sample without hunting. The
emulated pacing is set by --speed (emulated seconds per second of video) and
--fps; boot and binary-load waits are *fast-forwarded* (advanced without
capturing) so the video cuts straight to the action instead of showing ProDOS.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import threading
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable

ROOT = Path(__file__).resolve().parents[2]      # demo/capture/ -> repo root
DISK_DIR = ROOT / "build" / "disk"
OUT_DIR = Path(__file__).resolve().parent / "out"

# --- emulator timing -------------------------------------------------------
# The 6502 runs at ~1.0205 MHz, and the headless `run` verb takes KILOcycles
# (run 1500 ~= 1.5 emulated seconds). One frame therefore advances this many
# kcycles, scaled by --speed (emulated seconds shown per second of video).
KCYCLES_PER_SEC = 1020.5

# Boot/binary-load fast-forward: advance in big steps WITHOUT capturing until a
# sentinel appears on screen. Boot to the launcher is ~150-200M cycles.
FF_CHUNK_K = 20000          # 20M cycles per fast-forward step
FF_MAX_STEPS = 40           # up to ~800M cycles before we give up
HOLD_AFTER_FF_SEC = 1.5     # video seconds to hold the screen once a wait lands

# Human-typing pacing: keystrokes per second for the animated REPL/editor lines.
TYPE_CPS = 8.0

# --- Apple II key codes (what `key <code>` expects) ------------------------
RET, ESC, SPACE = 13, 27, 32
ARROW_LEFT, ARROW_RIGHT, ARROW_UP, ARROW_DOWN = 8, 21, 11, 10
SEL_UP, SEL_DOWN = ord("I"), ord("M")     # file-selector highlight (per legend)
SEL_IN, SEL_UP_DIR = ord("."), ord(",")   # enter folder / up a dir
CTRL = {c: ord(c) - 64 for c in "ABCDEFGHIJKLMNOPQRSTUVWXYZ"}   # Ctrl-A == 1 ...

# The program typed live into a new file in the editor (Act 1). Written in the
# *keyed* II+ form so the on-screen typing shows the digraphs that stand in for
# keys a 1978 keyboard lacks: <: :> = [ ], <% %> = { }, ??/ = \. It uses only
# Family-A features (array literal + subscript, a range `for`, interpolation),
# so it compiles and runs on the REPL disk. Output: "total = 14".
NEW_PROGRAM = [
    "let nums = <:3, 1, 4, 1, 5:>",
    "var total = 0",
    "for i in 0...4 <%",
    "  total = total + nums<:i:>",
    "%>",
    'print("total = ??/(total)")',
]


# --- izapple2 machine profiles (mirror emulator/run_izapple2.sh) -----------
PROFILE_FLAGS = {
    # -s3 empty: izapple2's default slot-3 'fastchip' makes the REPL's Videx
    # probe false-positive ("VIDEX 80 COLUMN DETECTED" in the banner) on a plain
    # Saturn machine; an empty slot 3 gives the clean "SwiftII ][+ Saturn" banner.
    "sat":   ["-model=2plus", "-s0", "saturn", "-s3", "empty"],
    "iienh": ["-model=2enh"],
    "iie":   ["-model=2e"],
}


@dataclass
class Act:
    num: int
    name: str
    profile: str                 # key into PROFILE_FLAGS
    disk1: str                   # boot disk basename in build/disk
    disk2: str | None            # drive-2 disk basename, or None
    run: Callable[["Director"], None]
    build_hint: str              # the `make` target that produces the disks


# ==========================================================================
# Headless protocol client (slim; the request/response stdin protocol).
# ==========================================================================
class Headless:
    PROMPT = b"* "

    def __init__(self, binary: str, args: list[str], cwd: Path):
        self._cwd = Path(cwd)
        self.proc = subprocess.Popen(
            [binary, *args], cwd=str(cwd),
            stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, bufsize=0)
        self._buf = bytearray()
        self._cond = threading.Condition()
        self._reader = threading.Thread(target=self._read_loop, daemon=True)
        self._reader.start()
        try:
            self._read_to_prompt(30)
            self.cmd("pause")            # force a known paused state before run
        except Exception:
            self.proc.kill()
            raise

    def _read_loop(self):
        while True:
            b = self.proc.stdout.read(1)
            if not b:
                with self._cond:
                    self._cond.notify_all()
                break
            with self._cond:
                self._buf += b
                self._cond.notify_all()

    def _read_to_prompt(self, timeout: float) -> str:
        deadline = time.time() + timeout
        with self._cond:
            while not self._buf.endswith(self.PROMPT):
                left = deadline - time.time()
                if left <= 0:
                    raise TimeoutError("no prompt; got: "
                                       + self._buf.decode("latin1")[-200:])
                self._cond.wait(left)
            out = bytes(self._buf[:-len(self.PROMPT)])
            self._buf.clear()
            return out.decode("latin1")

    def cmd(self, line: str, timeout: float = 120) -> str:
        self.proc.stdin.write((line + "\n").encode())
        self.proc.stdin.flush()
        return self._read_to_prompt(timeout)

    def run(self, kcycles: int):
        self.cmd(f"run {int(kcycles)}")

    def key(self, code: int):
        self.cmd(f"key {int(code)}")

    def type_(self, s: str):
        self.cmd(f"type {s}")

    def enter(self):
        self.cmd("enter")

    def text(self) -> str:
        # Strip the ANSI inverse-video escapes the headless `text` emits.
        raw = self.cmd("text")
        import re
        return re.sub(r"\x1b\[[0-9;]*m", "", raw)

    def png(self, dest: Path) -> bool:
        """Snapshot to `dest`. `png` writes ./snapshot.png in the cwd; very
        occasionally it doesn't land in time, so retry once. Returns success."""
        for _ in range(2):
            self.cmd("png")
            src = self._cwd / "snapshot.png"
            if src.exists():
                src.replace(dest)
                return True
        return False

    def close(self):
        try:
            self.proc.stdin.write(b"quit\n")
            self.proc.stdin.flush()
            self.proc.wait(timeout=10)
        except Exception:
            self.proc.kill()


# ==========================================================================
# Director — frame capture + the action vocabulary the acts are written in.
# ==========================================================================
class Director:
    def __init__(self, hl: Headless, frames_dir: Path, fps: int, speed: float,
                 verbose: bool = True):
        self.hl = hl
        self.frames_dir = frames_dir
        self.fps = fps
        self.kc_per_frame = max(1, round(KCYCLES_PER_SEC * speed / fps))
        self.frame = 0
        self.verbose = verbose

    # -- frame primitives ---------------------------------------------------
    def _grab(self):
        self.frame += 1
        dest = self.frames_dir / f"f{self.frame:06d}.png"
        if not self.hl.png(dest):
            # A snapshot hiccup must not leave a hole in the f000NNN sequence
            # (ffmpeg needs it contiguous): repeat the previous frame instead.
            prev = self.frames_dir / f"f{self.frame - 1:06d}.png"
            if prev.exists():
                shutil.copyfile(prev, dest)
            else:
                self.frame -= 1

    def hold(self, seconds: float):
        """Advance and capture at the configured cadence for `seconds` of video."""
        for _ in range(max(1, round(seconds * self.fps))):
            self.hl.run(self.kc_per_frame)
            self._grab()

    def skip(self, seconds: float):
        """Advance emulated time WITHOUT capturing (dead disk-seek time we don't
        want in the video). Used where the end screen can't be text-scraped to
        anchor a fast_forward (e.g. 80-column output)."""
        self.hl.run(round(KCYCLES_PER_SEC * seconds))

    def freeze(self, seconds: float):
        """Hold the CURRENT frame still (no emulation) — duplicates the last PNG."""
        if self.frame == 0:
            self._grab()
        last = self.frames_dir / f"f{self.frame:06d}.png"
        if not last.exists():
            return
        for _ in range(max(1, round(seconds * self.fps)) - 1):
            self.frame += 1
            shutil.copyfile(last, self.frames_dir / f"f{self.frame:06d}.png")

    # -- input + waiting ----------------------------------------------------
    def fast_forward(self, token: str, label: str = "",
                     settle: float = HOLD_AFTER_FF_SEC):
        """Advance in big steps WITHOUT capturing until `token` shows; cut there
        and hold the landing frame for `settle` video seconds."""
        self._log(f"  fast-forward -> {label or token!r}")
        up = token.upper()
        for _ in range(FF_MAX_STEPS):
            self.hl.run(FF_CHUNK_K)
            if up in self.hl.text().upper():
                self._grab()
                self.freeze(settle)
                return True
        self._log(f"  WARNING: never saw {token!r}; continuing")
        self._grab()
        return False

    def wait_for(self, token: str, max_seconds: float = 6.0, label: str = ""):
        """Capture in real time until `token` shows or max_seconds elapses."""
        self._log(f"  wait -> {label or token!r}")
        up = token.upper()
        for _ in range(max(1, round(max_seconds * self.fps))):
            self.hl.run(self.kc_per_frame)
            self._grab()
            if up in self.hl.text().upper():
                return True
        return False

    def press(self, code: int, settle: float = 0.5, label: str = ""):
        if label:
            self._log(f"  key {label}")
        self.hl.key(code)
        self.hold(settle)

    def type_line(self, text: str, settle: float = 1.2, label: str = ""):
        """Type a line at a human pace, one keystroke at a time, then Return.

        Keys go through `key` (the real keyboard path), NOT the `type` verb: the
        `type` verb drops a lone-space argument and translates each chunk on its
        own, so a per-char `type` loop turns "let n = 7" into "letn=7" and never
        folds the `??/` -> `\\` digraph. Raw key presses are exactly what a human
        does, so spaces survive and the II+ input layer's digraph + auto-lowercase
        state machine works keystroke by keystroke.

        `text` is the *keyed* form: lowercase letters are sent as their uppercase
        key (the II+ has no lowercase key; the input layer folds them back down),
        an uppercase letter is preceded by the `'` case marker, and `\\` is typed
        as the `??/` digraph (so pass "n=??/(n)", not "n=\\(n)")."""
        self._log(f'  type "{text}"{(" (" + label + ")") if label else ""}')
        self.hold(0.25)                      # a beat before the first keystroke
        for ch in text:
            if ch.isalpha():
                if ch.islower():
                    self.hl.key(ord(ch.upper()))      # auto-lowercased by the input layer
                else:
                    self.hl.key(ord("'")); self.hl.key(ord(ch))   # ' = uppercase marker
            else:
                self.hl.key(ord(ch))                  # digits, space, punctuation, ? ? /
            self.hold(self._keystroke_pause(ch))      # captures the char appearing
        self.hold(0.35)                      # a beat on the finished line
        self.hl.enter()
        self.hold(settle)

    def _keystroke_pause(self, ch: str) -> float:
        """Per-key dwell — a touch longer after a space or a `)`/`"` so the
        rhythm reads like typing rather than a metronome."""
        return 0.20 if ch in ' )"' else 1.0 / TYPE_CPS

    # -- file-selector helpers ---------------------------------------------
    def _highlight(self) -> str:
        """The entry name on the '>'-highlighted selector row (no '/' '*' marks)."""
        for line in self.hl.text().splitlines():
            if ">" in line:
                tail = line.split(">", 1)[1].strip().lstrip("*/").strip()
                if tail:
                    return tail.split()[0]
        return ""

    def _scan(self, want: str, step_code: int, max_steps: int) -> bool:
        """Step the highlight in one direction until `want` is selected or an end
        is hit. After each keypress, poll until the highlight actually moves —
        changing the selection triggers a preview-pane disk read, so the redraw
        lags the keypress by a variable delay; a fixed settle races it."""
        for _ in range(max_steps):
            cur = self._highlight().upper()
            if cur and cur.startswith(want):
                return True
            self.hl.key(step_code)
            for _ in range(15):                  # up to ~6M cycles for the move
                self.hl.run(400)
                if self._highlight().upper() != cur:
                    break
            self._grab()                         # one frame at the new position
            if self._highlight().upper() == cur:  # never moved -> end of list
                return cur.startswith(want)
        return False

    def select(self, name: str, max_steps: int = 40):
        """Move the selector highlight onto `name` (prefix match, any case).

        Scans down to the bottom, then up to the top, so it finds the entry
        regardless of where the highlight currently sits."""
        self._log(f"  select {name}")
        want = name.upper()
        if self._highlight().upper().startswith(want):
            return True
        if self._scan(want, SEL_DOWN, max_steps) or self._scan(want, SEL_UP, max_steps):
            return True
        self._log(f"  WARNING: '{name}' not found in the listing")
        return False

    def preview_load(self, settle: float = 1.3):
        """Page the highlighted file's source into the preview pane. The launcher
        only loads it after a long idle dwell (DWELL_TICKS; ~20M cycles under
        headless), so FAST-FORWARD through the dwell — cutting the dead wait —
        until the source (`//` comment header) shows, then hold it briefly."""
        self.fast_forward("//", label="preview loads", settle=settle)

    def editor_page_down(self, times: int):
        """Page the editor view DOWN past the comment header most sources start
        with (Ctrl-V), so actual code is on screen. The editor only scrolls the
        view on a *continuous* idle redraw, so each press is followed by a single
        continuous run (like fast_forward) — a chunked hold (a png every frame)
        never lets it reach that redraw."""
        self._log(f"  editor page-down x{times}")
        for _ in range(times):
            self.hl.key(CTRL["V"])
            self.hl.run(4000)            # continuous: let the view scroll
            self._grab()
            self.freeze(0.8)             # hold the new position so it reads

    def preview_scroll(self, pages: int = 3):
        """Read the highlighted file in the preview pane before opening it: load
        the preview, then page DOWN with `K` far enough to clear the long comment
        header most sources start with and reach actual code, rest on it, then a
        page up + down so the motion reads — ending ON the code, not back on the
        comments. `pages` is sized to the file's header (xgrdemo's is ~40 lines)."""
        self._log("  preview + scroll")
        self.preview_load(settle=0.7)
        for _ in range(pages):
            self.press(ord("K"), settle=0.5)    # K = page down toward the code
        self.hold(1.3)                           # rest on the code
        self.press(ord("J"), settle=0.6)         # a little back up...
        self.press(ord("K"), settle=0.6)         # ...and down again, ending on code
        self.hold(0.8)

    def _menu_highlight(self) -> str:
        """The launcher MENU's highlighted option line (its content starts with
        '>', e.g. '> 1  REPL'). Distinct from the file selector's '>' (mid-line),
        so a separate parse keeps it from matching the menu's legend row."""
        for line in self.hl.text().splitlines():
            content = line.strip().strip("#").strip()
            if content.startswith(">"):
                return content[1:].strip().upper()
        return ""

    def menu_select(self, label: str, max_steps: int = 8):
        """Pick a launcher-menu option by MOVING THE CURSOR (I/M) onto the line
        and pressing Return — not the 1-5 shortcut — so the selection is visible
        on screen. Adapts to either disk (REPL menu vs compiler menu)."""
        self._log(f"  menu-select {label}")
        up = label.upper()
        for _ in range(max_steps):
            if up in self._menu_highlight():
                self.press(RET, settle=0.5, label=f"Return = {label}")
                return
            cur = self._menu_highlight()
            self.hl.key(SEL_DOWN)                # M = down the menu
            for _ in range(8):
                self.hl.run(400)
                if self._menu_highlight() != cur:
                    break
            self._grab()
        self.press(RET, settle=0.5)             # fallback: take whatever is lit

    # Disk operations (open a volume, enter a folder, load/compile a .swift,
    # reload the listing) take ~8-12M emulated cycles each: izapple2 models real
    # Disk II seek timing. They are FAST-FORWARDED (advanced in big chunks
    # without capturing), so the video cuts through the seek grind instead of
    # filming 10 s of a spinning disk. The browser's '[X]EC' legend uniquely
    # means "a file listing is on screen" (gone in the picker / editor / while a
    # program runs), so it is the anchor for "the listing is back".
    SELECTOR_ANCHOR = "[X]EC"

    def enter_files(self, volume: str = "PRODOS"):
        """Open the file browser: the menu's File-selector option leads to a
        DISKS/VOLUMES picker (`/PRODOS.2.4.3`, `/SWIFTII.DATA`, //e `/RAM`); pick
        a volume by name and RET into its root listing."""
        self.menu_select("FILE")             # cursor to the File-selector option
        self.fast_forward("VOLUMES", label="volume picker", settle=0.6)
        if "VOLUMES" in self.hl.text().upper():
            self.select(volume)
            self.press(RET, settle=0.2, label=f"open volume {volume}")
            self.fast_forward(self.SELECTOR_ANCHOR, label=f"{volume} listing")

    def enter_folder(self, folder: str, anchor: str):
        """Highlight `folder`, enter it with '.', and fast-forward the directory
        read until `anchor` (an entry known to live inside the folder) appears."""
        self.select(folder)
        self.press(SEL_IN, settle=0.2, label=f". enter {folder}")
        self.fast_forward(anchor, label=f"{folder}/ listing")

    def run_sample(self, name: str, hold_secs: float, wait_token: str,
                   has_pause: bool = True):
        """Run the highlighted .swift with [X]: fast-forward the compile/run,
        hold on its output, then return to the browser.

        Running a .swift from the browser hands off to the interpreter and the
        program ends at the SWIFTSAT REPL `>` prompt; `:quit` cold-reboots the
        launcher, which restores the file listing the program was run from. So
        the round trip is X -> (run) -> :quit -> restored listing."""
        self.press(ord("X"), settle=0.3, label=f"X run {name}")
        self.fast_forward(wait_token, label=f"{name} output", settle=0.3)
        self.hold(hold_secs)
        if has_pause:                        # release the program's readLine hold
            self.press(RET, settle=0.4, label="release pause")
        self.skip(2.5)                       # program ends -> REPL '>' prompt
        self.type_line(":quit", settle=0.3, label=":quit -> launcher")
        self.fast_forward(self.SELECTOR_ANCHOR, label="back in selector (restored)")

    def run_snake(self, speed: str = "5"):
        """Run xsnake (an interactive lo-res game) from the browser: [X] shows the
        instructions and waits for a speed digit 1-9; that digit starts the run.
        Unsteered the trail heads right (dx=1) into the wall, so it plays out on
        its own and ends `crashed! trail length N`, then drops to the `>` prompt —
        same X -> (run) -> :quit -> restored-listing round trip as run_sample, but
        with a real-time capture of the gameplay instead of a fast-forward."""
        self.press(ord("X"), settle=0.3, label="X run XSNAKE")
        self.fast_forward("SET THE SPEED", label="snake instructions", settle=0.7)
        self.press(ord(speed), settle=0.4, label=f"speed {speed} = start")
        self.wait_for("CRASHED", max_seconds=8.0, label="snake plays -> crash")
        self.hold(2.2)                       # rest on `crashed! trail length N`
        self.skip(2.0)                       # program ends -> REPL '>' prompt
        self.type_line(":quit", settle=0.3, label=":quit -> launcher")
        self.fast_forward(self.SELECTOR_ANCHOR, label="back in selector (restored)")

    def run_graphics_demo(self, name: str, per_frame_k: int = 450,
                          max_frames: int = 160):
        """Run an auto-advancing lo-res GR showcase (xgrdemo) through the Family B
        compiler+runner and capture its scenes SPED UP. [X] streams + compiles the
        oversize .swift off the data disk, then the Runner paints five self-
        advancing scenes and ends `checksum = N` / `press any key`. The compile is
        dead disk-seek time, so fast-forward through it to the first scene (the
        runtime-only `NEXT IN` pause countdown is the unambiguous "Runner is
        painting" marker — scene NAMES also sit in the on-screen source preview, so
        they false-match), then sample the ~53 M cycle run at per_frame_k
        kcycles/frame (coarser than the global cadence, so the whole demo
        compresses to a few seconds of video) until the finale drops back to text.
        Mirrors tools/host/screenshots shot_grdemo. Ends on the checksum (each act
        is a fresh emulator session, so no :quit round trip is needed — like the
        original xbig act, the act just closes on the verdict)."""
        self.press(ord("X"), settle=0.3, label=f"X compile + run {name}")
        self.fast_forward("NEXT IN", label=f"{name} compile -> scene 1", settle=0.3)
        for _ in range(max_frames):          # fast-sample the auto-advancing scenes
            self.hl.run(per_frame_k)
            self._grab()
            up = self.hl.text().upper()
            if "CHECKSUM" in up or "PRESS ANY" in up:
                break
        self.hold(4.0)                       # end on `checksum = N`

    def to_menu(self):
        """Normalise to the launcher menu (selector -> Q; the menu has no parent)."""
        self.hl.key(ord("Q"))
        self.hl.run(2000)
        self.hl.key(ESC)
        self.hold(0.6)

    def _log(self, msg: str):
        if self.verbose:
            print(msg)


# ==========================================================================
# The acts — each mirrors a section of demo/VIDEO-DEMO.md.
# ==========================================================================
def act1(d: Director):
    """Act 1 — REPL, file selector, samples, 40-col editor (II+ Saturn)."""
    d.fast_forward("REPL", label="launcher menu")     # boot to the menu
    d.to_menu()
    d.freeze(1.0)

    # REPL: one sum, then a string + interpolation. (Menu pick by cursor.)
    d.menu_select("REPL")
    d.fast_forward(":HELP", label="REPL banner (SWIFTSAT loads)")
    d.type_line("40+2", label="a sum -> 42")
    d.type_line("let n = 7")
    d.type_line('print("n=??/(n)")', label="interpolation -> n=7")
    d.freeze(1.0)
    d.type_line(":quit", settle=0.4)                  # back to the launcher
    d.fast_forward("REPL", label="back at the menu")
    d.to_menu()

    # File selector + preview pane, then run a sample.
    d.enter_files(volume="PRODOS")                    # boot disk -> file listing
    d.enter_folder("XSAMPLES", anchor="XGRAPHICS")

    # Browse the samples: the preview pane renders real Swift off the disk for
    # each highlighted file — the "language off the disk" beat, no runs needed.
    for sample in ("XGRAPHICS", "XSNAKE", "XSPEAKER", "XVTAB"):
        d.select(sample)
        d.preview_load(settle=1.4)                    # page the source into view

    # Run xsnake to prove execution — read its source in the preview pane (scroll
    # it) first, then play it: an interactive lo-res game on a 1979 machine.
    d.select("XSNAKE")
    d.preview_scroll()
    d.run_snake(speed="5")

    # 40-column editor: write a NEW program from scratch, digraphs and all,
    # then save and run it. New scratch files open in [DGR] (cooked) mode, so
    # the `<: :>`, `<% %>` and `??/` digraphs fold to `[ ] { } \` as typed.
    d.press(ord("F"), settle=0.4, label="F = new file")
    d.fast_forward("DGR", label="empty editor (digraph mode)")
    d.hold(1.2)
    for line in NEW_PROGRAM:
        d.type_line(line, settle=0.5)                 # Return = newline in the editor
    d.hold(2.5)                                        # admire the finished program
    d.press(CTRL["S"], settle=0.4, label="Ctrl-S save")
    d.fast_forward("SAVE AS", label="save-as prompt")
    d.type_line("demo.swift", settle=0.5, label="filename")
    d.hold(2.0)                                        # the save writes (no reboot)
    d.press(CTRL["R"], settle=0.4, label="Ctrl-R save + run")
    d.fast_forward("= 14", label="program runs -> total = 14")
    d.hold(3.5)


def act2(d: Director):
    """Act 2 — compiler + runner: xgrdemo streamed off the data disk. A lo-res
    graphics program too big for RAM, compiled to bytecode and run scene by
    scene — same toolchain that runs xbig's checksum, here painting graphics."""
    d.fast_forward("FILE", label="compiler launcher menu")
    d.to_menu()
    d.enter_files(volume="SWIFTII")                    # the data disk /SWIFTII.DATA
    d.enter_folder("XSAMPLES", anchor="XGRDEMO")
    d.select("XGRDEMO")
    d.preview_scroll(pages=6)                           # ~40-line header before code
    d.run_graphics_demo("XGRDEMO")                      # compile (streamed) + paint scenes


def act3(d: Director):
    """Act 3 — the editor at 80 columns, then run xvtab (//e). Boots into 80-col."""
    # On a fresh disk the first boot ASKS the 80-col question on camera; the
    # helper holds on the prompt, answers Y, lets the keep-countdown tick, keeps.
    d.fast_forward_answering_80col()
    d.freeze(1.5)
    d.to_menu()
    d.enter_files(volume="PRODOS")                    # boot disk -> file listing
    d.enter_folder("XSAMPLES", anchor="XVTAB")
    d.select("XVTAB")
    d.preview_scroll()                                # read it in the preview first
    d.press(ord("E"), settle=0.3, label="E edit (80 col)")
    d.fast_forward("SAVE", label="editor @80")
    d.hold(1.5)                                        # the comment header at 80 col
    d.editor_page_down(3)                              # scroll past it to the code
    d.hold(1.5)                                        # actual code at 80 col
    d.press(CTRL["W"], settle=0.8, label="Ctrl-W -> 40 col")
    d.hold(3.0)                                        # same code clips/wraps at 40
    d.press(CTRL["W"], settle=0.8, label="Ctrl-W -> 80 col")
    d.hold(2.0)
    d.press(CTRL["Q"], settle=0.5, label="Ctrl-Q back to browser")
    d.fast_forward(d.SELECTOR_ANCHOR, label="back in selector")
    d.hold(1.0)

    # Payoff: RUN xvtab. Its bracketed labels land at the exact 80-column row/col
    # each one names — vtab()/htab() verified across all 80 columns, the
    # program's whole purpose and a stronger 80-col proof than its source alone.
    # xvtab has no readLine pause: after the last print it returns straight to
    # the interpreter's `>` prompt (its "stays up until you continue" line is
    # informational), so resting on it would leave the prompt's blinking cursor
    # sitting under the grid. run_sample shows the grid for the hold, then :quit
    # rounds back to the browser, so the act closes clean on the listing.
    d.select("XVTAB")
    d.run_sample("XVTAB", hold_secs=4.5, wait_token="CONTINUE", has_pause=False)


# Act-3 helper that needs the headless text channel (answers the 80-col prompt).
def _ff_answer_80col(self: Director) -> bool:
    """Fast-forward //e boot to the first-boot 'Use 80 columns?' question and
    SHOW the choice on camera: hold on the prompt, answer Y, let the keep
    countdown tick, press a key to keep, then land on the (wide) launcher menu.

    The keep step is timing-critical: after `Y` the launcher shows a ~10 s
    "press any key to keep 80 columns" countdown and reverts to 40 if nothing is
    pressed. One coarse FF chunk (~20 s of emulated time) overshoots that whole
    window, so the keypress never lands and the act runs in 40 columns. So reach
    the question COARSELY (it is just disk-seek time), then step FINELY into the
    countdown so the keypress lands inside it."""
    self._log("  //e boot -> show the 80-col choice (Y, keep)")
    for _ in range(FF_MAX_STEPS):                # coarse: disk-seek to the question
        self.hl.run(FF_CHUNK_K)
        if "USE 80 COLUMN" in self.hl.text().upper():
            break
    self._grab()
    self.freeze(2.5)                             # SHOW 'Use 80 columns? [Y]/[N]'
    self.hl.key(ord("Y"))
    for _ in range(80):                          # fine: reach the keep countdown
        self.hl.run(1500)                        # ~1.5 s, well inside the ~10 s window
        if "KEEP 80" in self.hl.text().upper():
            break
    self.hold(2.0)                               # SHOW the countdown ticking
    self.hl.key(SPACE)                           # any key keeps 80 columns
    self.hold(0.6)
    for _ in range(FF_MAX_STEPS):                # to the now-80-col launcher menu
        up = self.hl.text().upper()
        if "REPL" in up or "FILE" in up:
            break
        self.hl.run(FF_CHUNK_K)
    self._grab()
    self.freeze(HOLD_AFTER_FF_SEC)
    return True


Director.fast_forward_answering_80col = _ff_answer_80col


ACTS = [
    Act(1, "REPL, selector, samples, 40-col editor", "sat",
        "swiftii-iip-sat-repl.po", "swiftii-data.po", act1,
        "make disk-iip-sat-repl disk-data"),
    Act(2, "compiler + runner (xgrdemo)", "sat",
        "swiftii-iip-sat-compiler.po", "swiftii-data.po", act2,
        "make disk-iip-sat-compiler disk-data"),
    Act(3, "editor 40 and 80 columns", "iienh",
        "swiftii-iie-aux-repl.po", "swiftii-data.po", act3,
        "make disk-iie-aux-repl disk-data"),
]


# ==========================================================================
# Driver: per-act headless session, frame capture, ffmpeg encode.
# ==========================================================================
def find_headless() -> str | None:
    env = os.environ.get("IZAPPLE2_HEADLESS")
    if env and Path(env).exists():
        return env
    on_path = shutil.which("headless") or shutil.which("izapple2headless")
    if on_path:
        return on_path
    try:
        gopath = subprocess.check_output(["go", "env", "GOPATH"], text=True).strip()
        cand = Path(gopath) / "bin" / "headless"
        if cand.exists():
            return str(cand)
    except Exception:
        pass
    cand = ROOT / "headless"
    return str(cand) if cand.exists() else None


def disk_args(act: Act) -> tuple[list[str], list[str]]:
    """izapple2 flags + a list of missing disks (basenames)."""
    missing = []
    d1 = DISK_DIR / act.disk1
    if not d1.exists():
        missing.append(act.disk1)
    spec = f"diskii,disk1={d1}"
    if act.disk2:
        d2 = DISK_DIR / act.disk2
        if not d2.exists():
            missing.append(act.disk2)
        spec += f",disk2={d2}"
    return PROFILE_FLAGS[act.profile] + ["-s6", spec], missing


def encode(frames_dir: Path, out_mp4: Path, fps: int) -> bool:
    if not shutil.which("ffmpeg"):
        print("  ffmpeg not found — leaving frames un-encoded.")
        return False
    out_mp4.parent.mkdir(parents=True, exist_ok=True)
    cmd = ["ffmpeg", "-y", "-framerate", str(fps),
           "-i", str(frames_dir / "f%06d.png"),
           "-vf", "scale=560:384:flags=neighbor,format=yuv420p",   # 2x crisp
           "-c:v", "libx264", "-preset", "veryfast", "-crf", "18",
           str(out_mp4)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stderr[-800:])
        return False
    return True


def run_act(act: Act, binary: str, fps: int, speed: float, out_dir: Path,
            keep_frames: bool, do_encode: bool) -> Path | None:
    flags, missing = disk_args(act)
    if missing:
        print(f"Act {act.num}: missing disk(s): {', '.join(missing)}")
        print(f"  build them with:  {act.build_hint}")
        return None

    frames_dir = out_dir / f"act{act.num}_frames"
    if frames_dir.exists():
        shutil.rmtree(frames_dir)
    frames_dir.mkdir(parents=True)

    print(f"\n=== Act {act.num} — {act.name}  [{act.profile}] ===")
    hl = Headless(binary, flags, cwd=frames_dir)
    d = Director(hl, frames_dir, fps, speed)
    t0 = time.time()
    try:
        act.run(d)
    finally:
        hl.close()
    print(f"  captured {d.frame} frames in {time.time() - t0:.0f}s wall")

    out_mp4 = out_dir / f"act{act.num}.mp4"
    if do_encode and d.frame:
        if encode(frames_dir, out_mp4, fps):
            print(f"  wrote {out_mp4.relative_to(ROOT)}")
    if not keep_frames and do_encode and out_mp4.exists():
        shutil.rmtree(frames_dir)
    return out_mp4 if out_mp4.exists() else None


def concat(mp4s: list[Path], out: Path):
    if not (shutil.which("ffmpeg") and len(mp4s) > 1):
        return
    listing = out.parent / "concat.txt"
    listing.write_text("".join(f"file '{p.name}'\n" for p in mp4s))
    cmd = ["ffmpeg", "-y", "-f", "concat", "-safe", "0",
           "-i", str(listing), "-c", "copy", str(out)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    listing.unlink(missing_ok=True)
    if r.returncode == 0:
        print(f"\nFull demo: {out.relative_to(ROOT)}")
    else:
        print(r.stderr[-400:])


def smoke(binary: str, fps: int, speed: float, out_dir: Path):
    """Boot Act 1's disk to the menu and encode a ~3s clip — validates the
    headless -> PNG -> ffmpeg pipeline without running the full script."""
    act = ACTS[0]
    flags, missing = disk_args(act)
    if missing:
        print(f"smoke: missing {', '.join(missing)} — {act.build_hint}")
        return
    frames_dir = out_dir / "smoke_frames"
    if frames_dir.exists():
        shutil.rmtree(frames_dir)
    frames_dir.mkdir(parents=True)
    hl = Headless(binary, flags, cwd=frames_dir)
    d = Director(hl, frames_dir, fps, speed)
    try:
        d.fast_forward("REPL", label="launcher menu")
        d.to_menu()
        d.freeze(2.5)
    finally:
        hl.close()
    encode(frames_dir, out_dir / "smoke.mp4", fps)
    print(f"smoke: {d.frame} frames -> {(out_dir / 'smoke.mp4').relative_to(ROOT)}")


def main():
    global TYPE_CPS
    ap = argparse.ArgumentParser(description="Capture the SwiftII demo from the emulator.")
    ap.add_argument("--act", default="all", help="1, 2, 3, or all (default)")
    ap.add_argument("--fps", type=int, default=15,
                    help="output frame rate (default 15; raise for smoother motion)")
    ap.add_argument("--speed", type=float, default=1.0,
                    help="emulated seconds shown per second of video (default 1.0)")
    ap.add_argument("--type-cps", type=float, default=TYPE_CPS,
                    help=f"typing speed in characters/sec (default {TYPE_CPS:g})")
    ap.add_argument("--out", default=str(OUT_DIR), help="output directory")
    ap.add_argument("--keep-frames", action="store_true", help="keep the PNG frames")
    ap.add_argument("--no-encode", action="store_true", help="capture frames only")
    ap.add_argument("--smoke", action="store_true", help="boot+menu only (pipeline check)")
    ap.add_argument("--dry-run", action="store_true", help="print the plan, run nothing")
    args = ap.parse_args()
    TYPE_CPS = args.type_cps

    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)

    if args.dry_run:
        print("Demo capture plan (see demo/VIDEO-DEMO.md for the full script):")
        for a in ACTS:
            flags, missing = disk_args(a)
            tag = "  [disks missing]" if missing else ""
            print(f"  Act {a.num}: {a.name}")
            print(f"     profile {a.profile}  disks {a.disk1}"
                  f"{' + ' + a.disk2 if a.disk2 else ''}{tag}")
        print(f"\n  fps={args.fps} speed={args.speed} -> "
              f"{round(KCYCLES_PER_SEC * args.speed / args.fps)} kcycles/frame")
        return

    binary = find_headless()
    if not binary:
        print("error: izapple2 `headless` binary not found.\n"
              "  build it:  make acceptance-build   (or\n"
              "  go install github.com/ivanizag/izapple2/frontend/headless@latest)\n"
              "  then set IZAPPLE2_HEADLESS=/path/to/headless if it is not on PATH.",
              file=sys.stderr)
        sys.exit(1)

    if args.smoke:
        smoke(binary, args.fps, args.speed, out_dir)
        return

    if args.act == "all":
        chosen = ACTS
    else:
        chosen = [a for a in ACTS if str(a.num) == args.act]
        if not chosen:
            print(f"error: --act {args.act!r} (want 1, 2, 3, or all)", file=sys.stderr)
            sys.exit(2)

    made = []
    for a in chosen:
        mp4 = run_act(a, binary, args.fps, args.speed, out_dir,
                      args.keep_frames, not args.no_encode)
        if mp4:
            made.append(mp4)
    if len(made) > 1:
        concat(made, out_dir / "demo_full.mp4")


if __name__ == "__main__":
    main()
