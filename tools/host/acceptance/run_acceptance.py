#!/usr/bin/env python3
"""run_acceptance.py — drive the SwiftII emulator acceptance matrix unattended.

Backed by izapple2's `headless` frontend: embedded ROMs (nothing to source), the
same machine-config flags as izapple2sdl (`-model`, `-s0 saturn`, `-s6 diskii`),
and a deterministic stdin protocol — `run <cycles>` advances an exact number of
cycles then pauses, `key`/`type`/`enter` inject keystrokes, `text` dumps the
screen as ANSI, `png` snapshots it. That determinism (run-then-inspect, no
frame-timing guesswork) is why this is robust in CI.

For each hardware config it builds the right disk(s), boots izapple2 on that
machine, runs a scenario (the TESTRUN.SYSTEM 'Run tests' sweep, or a graphics / Videx
snapshot), scrapes the screen for the verdict, and — for the Family B compiler
disks — reads the per-test PASS/FAIL TESTLOG back off the data disk with
AppleCommander. Then it prints one pass/fail table across every config.

    python3 tools/host/acceptance/run_acceptance.py            # the whole matrix
    python3 tools/host/acceptance/run_acceptance.py iip sat     # just these configs
    python3 tools/host/acceptance/run_acceptance.py --list
    python3 tools/host/acceptance/run_acceptance.py --dry-run

Needs the `headless` binary (`make acceptance-build`, or `go install
github.com/ivanizag/izapple2/frontend/headless@latest`); set IZAPPLE2_HEADLESS
if it is not on PATH. AppleCommander/Java (already used by `make disks`) is only
needed for the Family B TESTLOG read-back.
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
import threading
import time
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]   # tools/host/acceptance/ -> repo root
DISK_DIR = ROOT / "build" / "disk"
DATA_PO = DISK_DIR / "swiftii-data.po"
AC_JAR = Path(os.environ.get("APPLECOMMANDER_JAR",
                             ROOT / "tools" / "host" / "AppleCommander-ac.jar"))


def _resolve_disk(disk_dir: Path, name: str) -> Path:
    """Resolve a canonical disk basename within disk_dir.

    Fresh build/disk/ images are un-versioned (swiftii-iip-lite-repl.po), but
    `make release` stages copies with a -v<version> suffix
    (swiftii-iip-lite-repl-v1.0.1.po). When the exact name is absent, fall back
    to a single <stem>-v*.po match so `RELEASE=releases/v1.0.1` still works.
    """
    exact = disk_dir / name
    if exact.exists():
        return exact
    matches = sorted(disk_dir.glob(f"{Path(name).stem}-v*.po"))
    return matches[0] if matches else exact

# izapple2 machine-config flag sets, mirroring emulator/run_izapple2.sh.
FLAGS = {
    "iip":   ["-model=2plus", "-s0", "language"],
    "sat":   ["-model=2plus", "-s0", "saturn"],
    "videx": ["-model=2plus", "-s0", "saturn", "-s3", "videx"],
    "iie":   ["-model=2e"],
    "iienh": ["-model=2enh"],
}


@dataclass
class Config:
    name: str          # harness id / results subdir
    flags: str         # key into FLAGS
    disk: str          # program .po basename (in build/disk)
    target: str        # make target that builds it
    scenario: str      # scenario builder name
    data: bool = True  # mount the data disk (TESTS/ + TESTRUN.SYSTEM) in drive 2


# The matrix — the E1-E8 rows of docs/testing/TESTING-emulators.md, plus the
# graphics / Videx snapshot configs.
CONFIGS: list[Config] = [
    # Keyboard-shortcut coverage across every UI mode (TESTING-keyboard.md):
    # launcher menu, About, Debug pager, file selector, REPL, editor. Run first —
    # it walks the whole UI, so it is the most interesting config to watch.
    Config("keyboard", "iip", "swiftii-iip-lite-repl.po", "disk-iip-lite-repl", "keyboard"),
    Config("keyboard-iie", "iie", "swiftii-iie-lite-repl.po", "disk-iie-lite-repl",
           "keyboard_iie"),
    # The user-facing oversize showcases (XSAMPLES/) on each Family B tier — the
    # TESTRUN compiler sweep (further down) runs TESTS/, not SAMPLES/, so this is
    # the only automated proof that xbig/xgrdemo/xfuncs compile+run where each
    # disk is meant to: xbig+xgrdemo on every tier, xfuncs paged-only (the two
    # FLAT disks — II+ and the //e-native non-aux — reject it by design). [X]-runs
    # each in the file browser.
    Config("samples", "iip", "swiftii-iip-compiler.po", "disk-iip-compiler",
           "samples_flat"),
    Config("samples-sat", "sat", "swiftii-iip-sat-compiler.po", "disk-iip-sat-compiler",
           "samples_paged"),
    Config("samples-iie", "iie", "swiftii-iie-compiler.po", "disk-iie-compiler",
           "samples_flat"),
    Config("samples-iie-aux", "iienh", "swiftii-iie-aux-compiler.po", "disk-iie-aux-compiler",
           "samples_paged"),
    # II+ digraph + case typing through the real editor: every C-digraph and
    # both case markers, the on-screen rendering as they're typed + after a
    # delete, the canonical bytes the ProDOS save writes (read back off the data
    # disk), and the rendering again after a reload. The //+ input/display model
    # (input.c + editor screen.c) is host-unit-tested, but only a real II+ run
    # exercises the keyboard -> gap buffer -> video RAM -> MLI save -> reload
    # chain end to end. II+ only — the //e disks pass the keyboard through
    # natively (no digraphs, no case markers), so there's nothing to verify there.
    Config("digraphs", "iip", "swiftii-iip-lite-repl.po", "disk-iip-lite-repl", "digraphs"),
    Config("iip", "iip", "swiftii-iip-lite-repl.po", "disk-iip-lite-repl", "sweep_repl"),
    Config("sat", "sat", "swiftii-iip-sat-repl.po", "disk-iip-sat-repl", "sweep_repl"),
    Config("sat-graphics", "sat", "swiftii-iip-sat-repl.po", "disk-iip-sat-repl",
           "graphics", data=False),
    Config("videx", "videx", "swiftii-iip-sat-repl.po", "disk-iip-sat-repl",
           "videx", data=False),
    Config("iie", "iie", "swiftii-iie-lite-repl.po", "disk-iie-lite-repl", "sweep_repl"),
    # NB: config name (→ build/acceptance/<name>/ dir) must avoid Windows reserved
    # device names — "aux" would break OneDrive sync on Windows, so it's "iie-aux".
    Config("iie-aux", "iienh", "swiftii-iie-aux-repl.po", "disk-iie-aux-repl", "sweep_repl"),
    Config("iie-aux-graphics", "iienh", "swiftii-iie-aux-repl.po", "disk-iie-aux-repl",
           "graphics", data=False),
    # NB: there is deliberately no //e *firmware 80-col* PNG config. Unlike the
    # Videx card (whose page is at least the main 40-col text), izapple2's headless
    # `png`/`text` can't render the //e firmware 80-col display — it interleaves
    # the 80 columns across main + aux RAM and headless captures only the main
    # page (every other column, garbled). So the //e 80-col REPL caret fix (commit
    # "repl///e: draw our own 80-col cursor block …") stays a manual / real-HW
    # eyeball check. The *other* half of the //e 80-col story — the editor's ^W
    # width toggle corrupting MLI — IS automated, as text, by `keyboard-iie`
    # (it toggles 40<->80<->40 in the editor and asserts the directory re-read
    # still lists a real entry; the symptom shows up back in scrapeable 40-col).
    Config("compiler", "iip", "swiftii-iip-compiler.po", "disk-iip-compiler", "sweep_fb"),
    Config("compiler-sat", "sat", "swiftii-iip-sat-compiler.po", "disk-iip-sat-compiler",
           "sweep_fb"),
    Config("compiler-iie", "iie", "swiftii-iie-compiler.po", "disk-iie-compiler", "sweep_fb"),
    Config("compiler-iie-aux", "iienh", "swiftii-iie-aux-compiler.po", "disk-iie-aux-compiler",
           "sweep_fb"),
    # The deliberately-failing ERRTESTS demos (TESTS/ERRTESTS/, datadisk): each
    # is meant to error, so the TESTRUN.SYSTEM 'Run tests' sweep skips them. This config
    # runs them by hand on a II+ Family B compiler disk ([X] in the file browser)
    # and asserts each triggers the on-target compile/runtime error display the
    # host suite can't reach.
    Config("errtests", "iip", "swiftii-iip-compiler.po", "disk-iip-compiler", "errtests"),
    # The REPL (Family A) counterpart — same demos, but [X] on a lite REPL disk
    # exercises the interpreter's own error display (repl.c), a separate path
    # from the Family B Compiler/Runner the `errtests` config above drives.
    Config("errtests-repl", "iip", "swiftii-iip-lite-repl.po", "disk-iip-lite-repl",
           "errtests_repl"),
]
BY_NAME = {c.name: c for c in CONFIGS}

# Rough wall-clock estimate (seconds) per scenario, used to show "time
# remaining" in the live window. The sweeps reboot once per on-disk test so
# they dominate; the snapshot/keyboard configs are short. These are only
# seeds — the harness rescales them by the ratio of actual:estimated as
# configs finish, so the ETA self-corrects over a run.
SCENARIO_EST = {
    "sweep_repl": 150,
    "sweep_fb": 180,
    "errtests": 210,
    "errtests_repl": 200,
    "graphics": 35,
    "videx": 35,
    "keyboard": 70,
    "keyboard_iie": 75,
    "digraphs": 120,
    "samples_flat": 90,
    "samples_paged": 90,
}


def _config_est(cfg: Config) -> float:
    return float(SCENARIO_EST.get(cfg.scenario, 90))


# Human-readable (short label, one-line explanation) per scenario — so the live
# window says what a config does instead of showing a cryptic id like sweep_fb.
SCENARIO_INFO = {
    "sweep_repl":   ("REPL sweep",
                     "Runs the on-disk self-checking suites (Family A / REPL), "
                     "one per reboot, reading each test's PASS/FAIL off the screen."),
    "sweep_fb":     ("compiler sweep",
                     "Runs the compiler + Runner suites (Family B), one per reboot, "
                     "reading each test's verdict back from the TESTLOG file."),
    "errtests":     ("error demos",
                     "Runs the deliberately-failing ERRTESTS demos on a Family B "
                     "compiler disk and checks each shows the right compile/runtime "
                     "error on target."),
    "errtests_repl": ("error demos (REPL)",
                     "Runs the same ERRTESTS demos on a REPL disk, checking the "
                     "interpreter's own compile/runtime error display (a separate "
                     "path from the compiler)."),
    "graphics":     ("graphics",
                     "Draws lo-res graphics from the REPL and snapshots a PNG to "
                     "eyeball — graphics can't be scraped as text."),
    "videx":        ("80-col",
                     "Smoke-tests text80() / Videx 80-column output and snapshots a PNG."),
    "keyboard":     ("keyboard",
                     "Walks the keyboard shortcuts in every UI mode: launcher menu, "
                     "About, Debug pager, file browser, editor, and REPL."),
    "keyboard_iie": ("keyboard //e",
                     "Keyboard-shortcut walk for the //e UI, incl. history recall "
                     "and the 80-column toggle."),
    "digraphs":     ("digraphs + case",
                     "Types every C-digraph (<% %> <: :> ??/ ??!) and both case "
                     "markers in the II+ editor; checks the on-screen rendering as "
                     "typed and after a delete, the canonical bytes the ProDOS save "
                     "writes (read back off the data disk), and the rendering after "
                     "a reload."),
    "samples_flat": ("samples (flat)",
                     "[X]-runs the oversize showcases on the flat II+ compiler: "
                     "xbig=6265, xgrdemo=1552, and xfuncs rejected by design."),
    "samples_paged": ("samples (paged)",
                     "[X]-runs the oversize showcases on a paged (Saturn / //e aux) "
                     "compiler: xbig=6265, xgrdemo=1552, xfuncs=210."),
}


def scenario_label(s: str) -> str:
    return SCENARIO_INFO.get(s, (s, ""))[0]


def scenario_desc(s: str) -> str:
    return SCENARIO_INFO.get(s, ("", ""))[1]

_ANSI = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]")

_KEY_NAMES = {3: "^C", 4: "^D", 8: "BS", 9: "TAB", 10: "LF",
              13: "RET", 27: "ESC", 32: "SPACE", 127: "DEL"}


def _key_label(code: int) -> str:
    """Human-readable label for an injected key code (for the live window)."""
    if code in _KEY_NAMES:
        return _KEY_NAMES[code]
    if 1 <= code <= 26:
        return "^" + chr(code + 64)   # control codes → ^A … ^Z (e.g. 20 -> ^T)
    if 33 <= code <= 126:
        return chr(code)
    return f"#{code}"


def _html_escape(s: str) -> str:
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


# Lines from the ProDOS 2.4.3 boot splash — not a meaningful "stage".
_SPLASH = ("ALL RIGHTS RESERVED", "PRODOS 8", "COPYRIGHT APPLE", "APPLE COMPUTER")


def _stage_label(plain: str) -> str:
    up = plain.upper()
    if any(s in up for s in _SPLASH):
        return "booting ProDOS…"
    return _last_nonblank(plain) or "…"


# Human-readable emulated-hardware spec per FLAGS profile (shown in the window).
HW_SPECS = {
    "iip":   "Apple ][+ · 64K (16K language card) · no extra cards",
    "sat":   "Apple ][+ · Saturn 128K RAM (slot 0)",
    "videx": "Apple ][+ · Saturn 128K (slot 0) + Videx Videoterm 80-col (slot 3)",
    "iie":   "Apple //e · 128K · firmware 80-col",
    "iienh": "Apple //e enhanced (65C02) · 128K (64K aux card)",
}


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


# --------------------------------------------------------------------------
# Live GUI window — mirrors izapple2's framebuffer (PNG snapshots) into a
# browser window that auto-refreshes, so you can watch the run without a
# terminal full of text. Stdlib-only (no tkinter/Pillow): a tiny localhost
# server + an <img> that reloads. Shows the real rendered screen incl. graphics.
# --------------------------------------------------------------------------

_LIVE_PAGE = """<!doctype html><meta charset=utf-8>
<title>SwiftII acceptance — live</title>
<style>
 :root{--accent:#4a90d9}
 html,body{margin:0;background:#111;height:100%;font:16px -apple-system,sans-serif;color:#ccc}
 body{padding:14px;box-sizing:border-box;height:100%;overflow:hidden}
 /* Three equal-height cards across the window: the emulated screen + live
    caption (DISPLAY, left), the streamed check log (MIDDLE), and the controls +
    test plan (RIGHT). Between each pair sits a full-height drag handle (.gutter,
    col-resize cursor anywhere along the seam): dragging it resizes the two panes
    it divides — one pane grows by exactly what its neighbour gives up, the third
    untouched. Widths are flex-grow WEIGHTS over a 0 basis, so the panes also
    reflow proportionally whenever the window itself is resized. */
 #main{display:flex;align-items:stretch;width:100%;height:100%;margin:0 auto}
 .card{background:#161616;border:1px solid #292929;border-radius:10px;
   padding:14px;box-sizing:border-box;min-height:0;overflow:hidden}
 /* The splitter between two panes: a thin centred line inside a wide hit-area,
    so the col-resize cursor is grabbable anywhere along the full-height seam. */
 .gutter{flex:0 0 14px;align-self:stretch;cursor:col-resize;position:relative}
 .gutter::before{content:"";position:absolute;top:0;bottom:0;left:50%;
   transform:translateX(-50%);width:2px;border-radius:2px;background:#2a2a2a}
 .gutter:hover::before,.gutter.drag::before{background:var(--accent);width:4px}
 /* LEFT card: weight 2 (the screen is the focus); the screen grows to fill, the
    caption is pinned beneath it. */
 #left{flex:2 1 0;min-width:260px;display:flex;flex-direction:column;gap:10px}
 #screen{flex:1 1 auto;min-height:0;display:flex;justify-content:center;align-items:center;
   position:relative;border:1px solid #333;border-radius:6px;background:#000;overflow:hidden}
 img{max-width:100%;max-height:100%;image-rendering:pixelated;display:block}
 #ph{position:absolute;color:#666;font-size:15px;text-align:center;padding:0 12px}
 /* The live caption sits directly under the screen it describes. */
 #live{flex:0 0 auto;width:100%;text-align:left;line-height:1.5}
 /* The caption lines below the screen are empty until a run starts; each reserves
    its populated height (min-height, in line units) so the screen keeps a
    CONSTANT size instead of shrinking the moment text fills in. */
 #config{font-weight:600;font-size:19px;color:#fff}
 #hw{color:#9aa;margin-top:2px;font-size:14px;min-height:1.5em}
 #scndesc{color:#8aa;font-size:14px;font-style:italic;margin-top:3px;min-height:4.5em}
 #stage{color:#7fd1b9;margin-top:8px;font-size:15px}
 #expect{color:#c9a227;margin-top:3px;font-size:14px;min-height:1.5em}
 #keys{color:#888;margin-top:3px;font-family:ui-monospace,Menlo,monospace;font-size:14px;
   word-break:break-all;min-height:3em}
 /* Times + progress, grouped directly under the keys line. The three times sit
    on one wrapping row; each value is a FIXED-WIDTH, tabular-nums box so a
    growing number (5s → 1m 23s) never shoves the next label along. */
 #status{margin-top:10px;border-top:1px solid #2a2a2a;padding-top:8px}
 #stats{display:flex;gap:8px 22px;flex-wrap:wrap;font-size:15px}
 .stat{display:flex;gap:7px;align-items:baseline;white-space:nowrap}
 .slab{color:#888}
 .sval{display:inline-block;min-width:7.5ch;color:#fff;font-variant-numeric:tabular-nums}
 #prog{height:6px;background:#222;border-radius:3px;margin:7px 0 4px;width:100%;overflow:hidden}
 #bar2{height:100%;width:0;background:var(--accent);transition:width .25s}
 /* The two progress labels (queue tally · scenario step) share one line. */
 #progline{display:flex;gap:6px 16px;flex-wrap:wrap;font-size:14px;min-height:1.5em}
 #pl{color:#888}
 #sub{color:#9aa;font-family:ui-monospace,Menlo,monospace}
 /* MIDDLE card: the streamed check log — every rec.check(), grouped by config,
    failures in red. A full-height column beside the display so it has room; the
    log fills the card and scrolls. Weight 1.4; drag either neighbouring gutter
    to make it wider or narrower. */
 #logcard{flex:1.4 1 0;min-width:220px;
   display:flex;flex-direction:column;min-height:0}
 #loghdr{flex:0 0 auto;font-weight:600;font-size:14px;color:#aaa;text-transform:uppercase;
   letter-spacing:.05em;border-bottom:1px solid #2a2a2a;padding-bottom:7px;margin-bottom:8px}
 #logsum{font-weight:600;text-transform:none;letter-spacing:0}
 #logsum.fail{color:#e06060}#logsum.ok{color:#46c074}
 #loghint{float:right;font-weight:400;text-transform:none;letter-spacing:0;
   color:#666;font-size:11px}
 #log{flex:1 1 auto;min-height:0;overflow:auto;
   font-family:ui-monospace,Menlo,monospace;font-size:13px;line-height:1.55}
 #log .empty{color:#666}
 #log .hdr{color:#9aa;margin:7px 0 2px;font-size:12px;letter-spacing:.03em;
   text-transform:uppercase}
 #log .hdr .fc{color:#e06060;font-weight:700;margin-left:6px}
 #log .row{display:flex;gap:8px;align-items:baseline}
 #log .row .g{font-weight:700;flex:0 0 auto}
 #log .row.pass{color:#7a8a99}#log .row.pass .g{color:#46c074}
 #log .row.fail{color:#f0c0c0}#log .row.fail .g{color:#e06060}
 #log .nm{flex:0 0 auto}
 #log .dt{color:#888;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
 #log .row.fail .dt{color:#d79a9a}
 /* RIGHT card: controls at the top, the plan scrolling to fill, legend pinned
    at the bottom — so the card always reads full-height alongside the screen.
    Weight 1; drag the gutter on its left to make it wider or narrower. */
 #panel{flex:1 1 0;min-width:260px;
   text-align:left;display:flex;flex-direction:column}
 #panelhdr{flex:0 0 auto;font-weight:600;font-size:14px;color:#aaa;text-transform:uppercase;
   letter-spacing:.05em;border-bottom:1px solid #2a2a2a;padding-bottom:7px;margin-bottom:10px}
 .lab{color:#888}.val{color:#eee}
 #ctl{margin-top:16px;display:flex;gap:10px;flex-wrap:wrap}
 button{font:15px -apple-system,sans-serif;color:#ddd;background:#222;border:1px solid #444;
   border-radius:6px;padding:6px 14px;cursor:pointer}
 button:hover{background:#2c2c2c}
 button.danger{border-color:#7a3a3a;color:#f0b0b0}
 button:disabled{opacity:.4;cursor:default}
 button.go{border-color:#2f7d4f;color:#bfe8cf}
 #sel{margin-top:10px;display:flex;gap:8px}
 #sel button{font-size:14px;padding:4px 12px}
 #hint{color:#777;font-size:13px;margin-top:8px}
 /* The plan: every config as a clickable checkbox-chip — click to tick/untick.
    Grows to fill the card and scrolls if the chips overflow. */
 #plan{margin-top:10px;flex:1 1 auto;min-height:0;overflow:auto;
   display:flex;gap:8px;flex-wrap:wrap;align-content:flex-start}
 .chip{padding:6px 13px;border-radius:15px;border:1px solid #333;background:#1a1a1a;
   font-size:14px;color:#999;display:flex;align-items:center;gap:7px;white-space:nowrap;
   cursor:pointer;user-select:none}
 .chip:hover{border-color:#666}
 .chip .mk{font-weight:700;font-variant-numeric:tabular-nums;font-size:20px;line-height:1}
 .chip.unchecked{opacity:.5}
 .chip.checked{border-color:#4a5a6a;color:#dde}
 .chip.checked .mk{color:#8fb0d0}
 .chip.queued{border-color:#3a557a;color:#cfe0f5}
 .chip.queued .mk{color:var(--accent)}
 .chip.running{border-color:var(--accent);color:#fff;background:#1d2a3a;
   box-shadow:0 0 0 1px var(--accent)}
 .chip.running .mk{color:var(--accent)}
 .chip.pass{border-color:#2f7d4f;color:#bfe8cf}
 .chip.pass .mk{color:#46c074}
 .chip.fail{border-color:#9a3636;color:#f0c0c0}
 .chip.fail .mk{color:#e06060}
 .chip.stopped{border-color:#8a7a30;color:#e6dca0}
 .chip.stopped .mk{color:#d4b94a}
 .chip .scn{color:#777;font-size:13px}
 /* Legend: what each scenario type actually does. Pinned below the plan. */
 #legend{flex:0 0 auto;margin:14px 0 0;font-size:13px;color:#888}
 #legend .ln{margin:4px 0}
 #legend b{color:#bbb;font-weight:600}
</style>
<div id=main>
<div id=left class=card>
 <div id=screen><img id=f style=display:none><div id=ph>press Start tests to begin…</div></div>
 <div id=live>
   <div id=config>SwiftII acceptance</div>
   <div id=hw></div>
   <div id=scndesc></div>
   <div id=stage>idle — nothing running yet</div>
   <div id=expect></div>
   <div id=keys></div>
   <!-- times + progress sit below the keys, with fixed-position values -->
   <div id=status>
     <div id=stats title="time-left figures are rough estimates; they self-correct as configs finish">
       <span class=stat><span class=slab>Elapsed</span><span class=sval id=elapsed>—</span></span>
       <span class=stat><span class=slab>Time left · config</span><span class=sval id=etaCfg>—</span></span>
       <span class=stat><span class=slab>· all</span><span class=sval id=etaAll>—</span></span>
     </div>
     <div id=prog><div id=bar2></div></div>
     <div id=progline><span id=pl></span><span id=sub></span></div>
   </div>
 </div>
</div>
<div class=gutter data-a=left data-b=logcard title="drag to resize the screen / log split"></div>
<div id=logcard class=card>
 <div id=loghdr>Check log <span id=logsum></span><span id=loghint>drag the dividers to resize</span></div>
 <div id=log><div class=empty>check results stream here as each test runs — failures in red…</div></div>
</div>
<div class=gutter data-a=logcard data-b=panel title="drag to resize the log / controls split"></div>
<div id=panel class=card>
 <div id=panelhdr>Test plan &amp; controls</div>
 <div id=ctl>
   <button id=start class=go onclick="cmd('start')">Start tests</button>
   <button id=stop class=danger onclick="stopCur()">Stop current test</button>
   <button id=stopall class=danger onclick="stopAll()">Stop all tests</button>
 </div>
 <div id=sel>
   <button onclick="cmd('checkall')">Select all</button>
   <button onclick="cmd('uncheckall')">Deselect all</button>
 </div>
 <div id=hint>Tick the configs to run, then press Start · click a config to
   tick / untick it · click the running one (or Stop current) to halt just it</div>
 <div id=plan></div>
 <div id=legend></div>
</div>
</div>
<script>
 const $=id=>document.getElementById(id), f=$('f');
 const MARK={pass:'\\u2713',fail:'\\u2717',running:'\\u25B6',stopped:'\\u23F9',
   checked:'\\u2611',unchecked:'\\u2610'};
 let RUNNING=null;
 async function cmd(action,name){
   try{await fetch('/cmd?action='+action+(name?'&name='+encodeURIComponent(name):''));}
   catch(e){}
 }
 function stopCur(){
   if(!RUNNING) return;
   if(confirm('Stop the running test "'+RUNNING+'"?\\nIt will be marked stopped and '
     +'the next queued config begins.')) cmd('stop');
 }
 function stopAll(){
   if(confirm('Stop ALL tests?\\nThe running one is marked stopped and the rest of '
     +'the queue is cleared.')) cmd('stopall');
 }
 function clickChip(c){
   if(c.status==='running'){ stopCur(); return; }
   cmd('toggle', c.name);          // tick if unticked, untick if ticked
 }
 function dur(s){
   s=Math.max(0,Math.round(s));
   const m=Math.floor(s/60); s=s%60;
   return m?m+'m '+String(s).padStart(2,'0')+'s':s+'s';
 }
 function fmt(end){
   if(!end) return '—';
   if(end-Date.now()/1000<=0) return 'finishing\\u2026';
   return '~'+dur(end-Date.now()/1000);   // ~ marks it as a rough estimate
 }
 function elapsed(start,frozen){
   if(frozen!=null) return dur(frozen);   // batch finished — clock stopped
   if(!start) return '—';                  // idle, no run started yet
   return dur(Date.now()/1000-start);      // live count since Start
 }
 let chipSig='';   // only rebuild the chips when something actually changed
 function chips(list){
   const sig=JSON.stringify((list||[]).map(c=>[c.name,c.status,c.q]));
   if(sig===chipSig) return;
   chipSig=sig;
   const el=$('plan'); el.textContent='';
   for(const c of (list||[])){
     const st=c.status||'unchecked';
     const d=document.createElement('div'); d.className='chip '+st;
     const mk=(c.q>0)?('#'+c.q):(MARK[st]||'');   // run-order # if it'll run, else glyph
     d.innerHTML='<span class=mk>'+mk+'</span>'
       +'<span>'+c.name+'</span><span class=scn>'+(c.scn||c.scenario||'')+'</span>';
     const act=(st==='running')?'click to stop this test'
       :(st==='queued')?'queued — click to untick'
       :(st==='checked')?'ticked — click to untick':'click to tick this config';
     d.title=(c.desc?c.desc+'\\n':'')+act;   // hover shows what this scenario does
     d.onclick=()=>clickChip(c);
     el.appendChild(d);
   }
 }
 let legendSig='';   // explain each scenario type once (memoized)
 function legend(list){
   const seen={}, order=[];
   for(const c of (list||[])){
     if(!(c.scenario in seen)){ seen[c.scenario]={scn:c.scn,desc:c.desc}; order.push(c.scenario); }
   }
   const sig=order.join(',');
   if(sig===legendSig) return;
   legendSig=sig;
   const el=$('legend'); el.textContent='';
   for(const s of order){
     const d=document.createElement('div'); d.className='ln';
     d.innerHTML='<b>'+(seen[s].scn||s)+'</b> — '+(seen[s].desc||'');
     el.appendChild(d);
   }
 }
 function esc(s){return (s==null?'':String(s)).replace(/&/g,'&amp;')
   .replace(/</g,'&lt;').replace(/>/g,'&gt;');}
 let logSig=-1;   // only rebuild the log when a new check has streamed in
 function renderLog(list,seq){
   if(seq===logSig) return;
   logSig=seq;
   const el=$('log'), sum=$('logsum');
   if(!list||!list.length){
     el.innerHTML='<div class=empty>check results stream here as each test runs '
       +'\\u2014 failures in red\\u2026</div>';
     sum.textContent=''; sum.className=''; return;
   }
   // stay pinned to the newest line unless the user has scrolled up to read back
   const atBottom = el.scrollHeight-el.scrollTop-el.clientHeight < 30;
   const groups=[]; let cur=null, fails=0;
   for(const e of list){                       // group consecutive checks by config
     if(!cur||cur.cfg!==e.cfg){ cur={cfg:e.cfg,rows:[],fails:0}; groups.push(cur); }
     cur.rows.push(e); if(!e.ok){ cur.fails++; fails++; }
   }
   let html='';
   for(const g of groups){
     const fc=g.fails?('<span class=fc>'+g.fails+' failed</span>'):'';
     html+='<div class=hdr>'+esc(g.cfg)+fc+'</div>';
     for(const e of g.rows){
       const cls=e.ok?'pass':'fail', gl=e.ok?'\\u2713':'\\u2717';
       html+='<div class="row '+cls+'"><span class=g>'+gl+'</span>'
         +'<span class=nm>'+esc(e.name)+'</span>'
         +'<span class=dt>'+esc(e.detail||'')+'</span></div>';
     }
   }
   el.innerHTML=html;
   sum.textContent=fails?('\\u00b7 '+fails+' failed'):'\\u00b7 all passing';
   sum.className=fails?'fail':'ok';
   if(atBottom) el.scrollTop=el.scrollHeight;
 }
 let lastFrame=-1;
 setInterval(async()=>{
   try{const j=await(await fetch('/status?t='+Date.now())).json();
     RUNNING=j.running||null;
     const active=!!RUNNING||(j.n_queued||0)>0;   // a run is in progress
     if((j.frame||0)!==lastFrame){               // reload the screen only on a new frame
       lastFrame=j.frame||0;
       if(lastFrame>0){ f.src='/frame.png?t='+lastFrame; f.style.display=''; $('ph').style.display='none'; }
     }
     $('config').textContent=j.config||'SwiftII acceptance';
     $('hw').textContent=j.hw||'';
     $('scndesc').textContent=j.scndesc||'';
     $('elapsed').textContent=elapsed(j.el_start,j.el_frozen);
     $('etaCfg').textContent=fmt(j.cfg_end);
     $('etaAll').textContent=fmt(j.all_end);
     $('stage').innerHTML=j.stage?('<span class=lab>stage:</span> <span class=val>'+j.stage+'</span>'):'';
     $('expect').innerHTML=j.expect?('<span class=lab>looking for:</span> '+j.expect):'';
     $('keys').innerHTML=j.keys?('<span class=lab>keys:</span> '+j.keys):'';
     $('sub').textContent=j.sub||'';
     const p=j.prog||{};
     $('bar2').style.width=(p.total?100*p.done/p.total:0)+'%';
     $('pl').textContent=p.label||'';
     $('start').disabled=active;
     $('stop').disabled=!RUNNING;
     $('stopall').disabled=!active;
     chips(j.configs);
     legend(j.configs);
     renderLog(j.log, j.log_seq);
     document.title=(j.config||'live')+(j.stage?(' — '+j.stage):'');
   }catch(e){}
 },250);
 // Splitter gutters: dragging one resizes only the two panes it sits between.
 // Pane widths are flex-grow weights over a 0 basis; we convert the drag delta
 // into pixels, clamp to each pane's min-width, and split the pair's COMBINED
 // weight by the new pixel ratio — so one grows by exactly what the other loses
 // and the third pane is left alone. Weights (not px) keep it responsive: the
 // browser re-divides on window resize on its own.
 function setupSplitters(){
   for(const g of document.querySelectorAll('.gutter')){
     g.addEventListener('mousedown',e=>{
       e.preventDefault();
       const a=$(g.dataset.a), b=$(g.dataset.b);
       const wA0=a.getBoundingClientRect().width, wB0=b.getBoundingClientRect().width;
       const W=wA0+wB0, startX=e.clientX;
       const G=(parseFloat(getComputedStyle(a).flexGrow)||1)
              +(parseFloat(getComputedStyle(b).flexGrow)||1);
       const minA=parseFloat(getComputedStyle(a).minWidth)||120;
       const minB=parseFloat(getComputedStyle(b).minWidth)||120;
       g.classList.add('drag');
       document.body.style.cursor='col-resize';
       document.body.style.userSelect='none';
       const move=ev=>{
         let wA=Math.max(minA,Math.min(W-minB,wA0+(ev.clientX-startX)));
         a.style.flexGrow=G*wA/W;          // pair's weight, split by the new ratio
         b.style.flexGrow=G*(W-wA)/W;
       };
       const up=()=>{
         window.removeEventListener('mousemove',move);
         window.removeEventListener('mouseup',up);
         g.classList.remove('drag');
         document.body.style.cursor='';
         document.body.style.userSelect='';
       };
       window.addEventListener('mousemove',move);
       window.addEventListener('mouseup',up);
     });
   }
 }
 setupSplitters();
</script>
"""


class LiveWindow:
    """Serves the framebuffer PNG + a JSON status to a browser window, and is
    also the run's **controller**: it owns the queue of configs to run, lets the
    page stop the running one or queue/dequeue any config by clicking its chip,
    and computes the (self-correcting) time-remaining estimates the page counts
    down to. The driver loop (`run_interactive`) pulls work from it."""

    def __init__(self, all_configs: list):
        import http.server
        import collections
        self._frame = b""
        self._frame_seq = 0             # bumped per frame so the page reloads only on change
        self._lock = threading.Lock()
        self._opened = False
        self._meta = {c.name: c for c in all_configs}   # name -> Config (order kept)
        # Controller state (all under _lock):
        self._checked: set[str] = set()  # configs the user has ticked to run
        self._started = False           # a run is active (user pressed Start)
        self._queue: list[str] = []      # configs left to run this run, in order
        self._running: str | None = None
        self._done: dict[str, str] = {}  # name -> verdict (pass/fail/stopped)
        self._run_start = 0.0            # epoch the user pressed Start (0 = idle)
        self._run_elapsed = None         # frozen wall-clock once the batch finishes
        self._cfg_end = 0.0              # epoch the current config est-finishes
        self._scale = 1.0               # actual:estimated ratio of finished configs
        self._act_sum = 0.0             # sum of finished (non-stopped) durations
        self._est_sum = 0.0             # sum of their estimates — _scale = act/est
        self._stop_name: str | None = None   # a stop request the driver will consume
        self._active_hl = None          # the running Headless, for stop() to kill
        # Display fields (config label, hw, scenario desc, stage, looking-for, keys):
        self._st = {"config": "", "hw": "", "scndesc": "",
                    "stage": "", "expect": "", "sub": ""}
        self._keys = collections.deque(maxlen=14)
        # Streamed check log — every rec.check() across the run, so a red chip's
        # *why* (which check failed, with its detail) shows in the window, not
        # just the terminal. Grouped by config on the page.
        self._log: collections.deque = collections.deque(maxlen=400)
        self._log_seq = 0               # bumped per entry so the page rebuilds only on change
        win = self

        class Handler(http.server.BaseHTTPRequestHandler):
            def log_message(self, *a):          # keep the terminal clean
                pass

            def _send(self, code, ctype, body=b""):
                self.send_response(code)
                self.send_header("Content-Type", ctype)
                self.send_header("Cache-Control", "no-store")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                if body:
                    self.wfile.write(body)

            def do_GET(self):
                if self.path.startswith("/frame.png"):
                    with win._lock:
                        data = win._frame
                    self._send(200 if data else 204, "image/png", data)
                elif self.path.startswith("/status"):
                    import json
                    self._send(200, "application/json",
                               json.dumps(win._payload()).encode())
                elif self.path.startswith("/cmd"):
                    from urllib.parse import urlparse, parse_qs
                    q = parse_qs(urlparse(self.path).query)
                    win._command(q.get("action", [""])[0], q.get("name", [""])[0])
                    self._send(204, "text/plain")
                else:
                    self._send(200, "text/html; charset=utf-8", _LIVE_PAGE.encode())

        self._httpd = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.port = self._httpd.server_address[1]
        threading.Thread(target=self._httpd.serve_forever, daemon=True).start()
        print(f"acceptance: live window at http://127.0.0.1:{self.port}/")

    # -- estimate helpers --------------------------------------------------
    def _est(self, name: str) -> float:
        c = self._meta.get(name)
        return _config_est(c) if c else 90.0

    def _payload(self) -> dict:
        """Build the JSON the page polls: display fields + every config's chip
        status, progress, and the (re-scaled) time-remaining estimates."""
        import time as _t
        with self._lock:
            now = _t.time()
            # Configs still to run: the live queue once started, else everything
            # the user has ticked (so the order numbers + total estimate show
            # before Start too — iip is #1 from the outset).
            pending = (self._queue if self._started else
                       [n for n in self._meta
                        if n in self._checked and n not in self._done])
            qpos = {n: i + 1 for i, n in enumerate(pending)}   # 1-based run order
            configs = []
            for name in self._meta:                  # every config, in matrix order
                if name == self._running:
                    status = "running"
                elif name in self._done:
                    status = self._done[name]        # pass / fail / stopped
                elif name in qpos:
                    # in the run order: "queued" once started, else just "ticked"
                    status = "queued" if self._started else "checked"
                elif name in self._checked:
                    status = "checked"               # ticked but not in this batch
                else:
                    status = "unchecked"
                scen = self._meta[name].scenario
                configs.append({"name": name, "scenario": scen,
                                "scn": scenario_label(scen), "desc": scenario_desc(scen),
                                "status": status, "q": qpos.get(name, 0)})
            q_rem = sum(self._est(n) for n in pending) * self._scale
            if self._running:
                all_end = self._cfg_end + q_rem
            elif pending:
                all_end = now + q_rem                # projection before Start
            else:
                all_end = 0.0
            done = len(self._done)
            total = done + (1 if self._running else 0) + len(pending)
            noun = "queued" if self._started else "selected"
            label = (f"{done} done · {1 if self._running else 0} running · "
                     f"{len(pending)} {noun}")
            return dict(self._st, keys=" ".join(self._keys), running=self._running or "",
                        started=self._started, n_queued=len(self._queue),
                        frame=self._frame_seq, cfg_end=self._cfg_end, all_end=all_end,
                        el_start=self._run_start, el_frozen=self._run_elapsed,
                        prog={"done": done, "total": total, "label": label},
                        configs=configs, log=list(self._log), log_seq=self._log_seq)

    # -- browser-driven commands ------------------------------------------
    def _command(self, action: str, name: str):
        # Ticking is pure selection — it never starts anything; only Start does.
        # Unticking a config mid-run does drop it from the remaining queue.
        if action == "toggle" and name in self._meta:
            with self._lock:
                if name == self._running:
                    return                       # use stop for the running one
                if name in self._checked:        # untick → also drop from the queue
                    self._checked.discard(name)
                    if name in self._queue:
                        self._queue.remove(name)
                else:                            # tick → (re)select; clear old verdict
                    self._checked.add(name)
                    self._done.pop(name, None)
        elif action in ("checkall", "uncheckall"):
            with self._lock:
                if action == "checkall":
                    self._checked = set(self._meta)
                else:
                    self._checked.clear()
                    self._queue.clear()      # drop the rest of the run (current keeps going)
        elif action == "start":
            import time as _t
            with self._lock:
                self._started = True
                self._run_start = _t.time()   # reset + start the elapsed clock
                self._run_elapsed = None
                self._log.clear()             # fresh check log for this run
                self._log_seq += 1
                # Run the whole ticked set fresh (clear prior verdicts), keeping
                # matrix order; skip whatever's mid-run.
                self._queue = [n for n in self._meta
                               if n in self._checked and n != self._running]
                for n in self._queue:
                    self._done.pop(n, None)
        elif action in ("stop", "stopall"):
            with self._lock:
                if action == "stopall":
                    self._started = False
                    self._queue.clear()          # abort the rest of the run
                if self._running:
                    self._stop_name = self._running
                hl = self._active_hl
            if hl:
                hl.stop()                        # kill outside the lock

    # -- driver-side (run_interactive) ------------------------------------
    def set_selection(self, configs: list):
        """Tick the given configs (the page starts with them all checked)."""
        with self._lock:
            self._checked = {c.name for c in configs}

    def next_config(self) -> str | None:
        """Pop the next queued config, mark it running, set its ETA — but only
        once the user has pressed Start. None while idle or queue-empty."""
        import time as _t
        with self._lock:
            if not self._started or not self._queue:
                return None
            name = self._queue.pop(0)
            self._running = name
            self._cfg_end = _t.time() + self._est(name) * self._scale
            return name

    def attach_headless(self, hl):
        with self._lock:
            self._active_hl = hl

    def finish_config(self, name: str, verdict: str, duration: float):
        with self._lock:
            self._active_hl = None
            self._running = None
            self._cfg_end = 0.0
            self._done[name] = verdict
            if verdict != "stopped":             # stopped configs ran partial time
                self._act_sum += duration
                self._est_sum += self._est(name)
                if self._est_sum > 0:
                    self._scale = self._act_sum / self._est_sum
            if not self._queue:                  # batch done → back to selection mode
                self._started = False            # (ticked configs renumber, Start re-runs)
                if self._run_start:              # freeze the elapsed clock at the finish
                    import time as _t
                    self._run_elapsed = _t.time() - self._run_start

    def consume_stop(self, name: str) -> bool:
        """True iff the user asked to stop *this* config (clears the request)."""
        with self._lock:
            if self._stop_name == name:
                self._stop_name = None
                return True
            return False

    # -- display setters ---------------------------------------------------
    def open(self):
        """Pop the browser. Opened up front (idempotent) so the user can tick
        configs and press Start — nothing runs until they do."""
        if self._opened:
            return
        self._opened = True
        import webbrowser
        webbrowser.open(f"http://127.0.0.1:{self.port}/")

    def update(self, png: bytes):
        with self._lock:
            self._frame = png
            self._frame_seq += 1

    def set_config(self, name: str, hw: str, desc: str = ""):
        with self._lock:
            self._st["config"], self._st["hw"] = name, hw
            self._st["scndesc"] = desc
            self._st["stage"] = self._st["expect"] = self._st["sub"] = ""
            self._keys.clear()

    def set_stage(self, text: str):
        with self._lock:
            self._st["stage"] = text

    def set_expect(self, text: str):
        with self._lock:
            self._st["expect"] = text

    def set_progress(self, done: int, total: int, label: str = ""):
        # Scenario-level sub-progress (e.g. "keyboard 3/30"); the overall bar is
        # the config queue, computed in _payload().
        with self._lock:
            self._st["sub"] = label or (f"{done}/{total}" if total else "")

    def add_key(self, label: str):
        with self._lock:
            self._keys.append(label)

    def add_log(self, config: str, ok: bool, name: str, detail: str = ""):
        """Append one check result (config, pass/fail, name, detail) to the
        streamed log the page renders. Called from Recorder.check()."""
        with self._lock:
            self._log.append({"cfg": config, "ok": bool(ok),
                              "name": name, "detail": detail})
            self._log_seq += 1

    def close(self):
        try:
            self._httpd.shutdown()
        except Exception:
            pass


# --------------------------------------------------------------------------
# izapple2 headless session — the stdin protocol client.
# --------------------------------------------------------------------------

class Headless:
    """Drives one `headless` process: send a command, read until its `* ` prompt.

    A background thread accumulates stdout so reads never deadlock; the protocol
    is request/response (every command ends by re-printing the prompt)."""

    PROMPT = b"* "

    def __init__(self, binary: str, args: list[str], cwd: Path, show: bool = False,
                 window=None):
        self.show = show              # echo izapple2's screen to the terminal live
        self.window = window          # LiveWindow to mirror frames/stage/keys into
        self._cwd = Path(cwd)
        self._last_shown = None
        self.proc = subprocess.Popen(
            [binary, *args], cwd=str(cwd),
            stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, bufsize=0)
        self._buf = bytearray()
        self._stopped = False         # set by stop() to abort a blocked read fast
        self._cond = threading.Condition()
        self._reader = threading.Thread(target=self._read_loop, daemon=True)
        self._reader.start()
        try:
            self._read_to_prompt(30)        # consume the first prompt
            # The machine can still be starting up when we issue the first `run`
            # (it races the Start goroutine), which `run` rejects as "already
            # running". An explicit pause forces a known paused state first.
            self.cmd("pause")
        except Exception:
            self.proc.kill()                # never leave an orphan holding the disk
            raise

    def _read_loop(self):
        while True:
            b = self.proc.stdout.read(1)
            if not b:
                with self._cond:        # wake any waiter on EOF (e.g. after stop)
                    self._cond.notify_all()
                break
            with self._cond:
                self._buf += b
                self._cond.notify_all()

    def _read_to_prompt(self, timeout: float) -> str:
        deadline = time.time() + timeout
        with self._cond:
            while not self._buf.endswith(self.PROMPT):
                if self._stopped:
                    raise RuntimeError("stopped by user")
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

    # protocol verbs
    def run(self, kcycles: int):
        self.cmd(f"run {kcycles}")

    def key(self, code: int):
        self._logkey(_key_label(code))
        self.cmd(f"key {code}")

    def type_(self, s: str):
        self._logkey('"' + s + '"')
        self.cmd(f"type {s}")

    def enter(self):
        self._logkey("RET")
        self.cmd("enter")

    def _logkey(self, label: str):
        if self.window:
            self.window.add_key(_html_escape(label))

    # Scenario-facing helpers to annotate the live window (no-op without one).
    def looking_for(self, text: str):
        if self.window:
            self.window.set_expect(_html_escape(text))

    def progress(self, done: int, total: int, label: str = ""):
        if self.window:
            self.window.set_progress(done, total, label)

    def text(self) -> str:
        raw = self.cmd("text")
        plain = _ANSI.sub("", raw)
        # The poll loops call text() many times; only act when the screen
        # actually changed (a clean trace + no needless PNG snapshots).
        key = "\n".join(l.rstrip() for l in plain.splitlines())
        changed = key != self._last_shown
        self._last_shown = key
        if changed and self.show:
            sys.stdout.write("\033[H\033[2J" + raw + "\n")   # redraw in place
            sys.stdout.flush()
        if changed and self.window:
            self.window.set_stage(_html_escape(_stage_label(plain)))
            self._mirror_frame()      # push a real framebuffer PNG to the window
        return plain

    def _mirror_frame(self):
        """Snapshot the framebuffer and push the PNG to the live window."""
        snap = self._cwd / "snapshot.png"
        try:
            self.cmd("png")
            if snap.exists():
                self.window.update(snap.read_bytes())
        except Exception:
            pass

    def png(self, dest: Path):
        # `png` ignores its filename arg and always writes ./snapshot.png in the
        # process CWD (which we set per-config), so move it to the wanted name.
        self.cmd("png")
        src = dest.parent / "snapshot.png"
        if src.exists():
            if self.window:
                self.window.update(src.read_bytes())   # mirror graphics snapshots too
            src.replace(dest)

    def repl_line(self, s: str):
        """Type a REPL line and let the interpreter consume + echo it."""
        self.type_(s)
        self.enter()
        self.run(1500)

    def stop(self):
        """Abort mid-scenario: kill the emulator and wake the blocked reader so
        the in-flight `cmd` raises promptly instead of waiting out its timeout."""
        self._stopped = True
        try:
            self.proc.kill()
        except Exception:
            pass
        with self._cond:
            self._cond.notify_all()

    def close(self):
        if self._stopped:               # already killed by stop()
            return
        try:
            self.proc.stdin.write(b"quit\n")
            self.proc.stdin.flush()
        except Exception:
            pass
        try:
            self.proc.wait(timeout=10)
        except Exception:
            self.proc.kill()


def _last_nonblank(screen: str) -> str:
    for line in reversed(screen.splitlines()):
        if line.strip().strip("#").strip():
            return line.strip().strip("#").strip()
    return ""


def menu_key(screen: str, label: str) -> str | None:
    """Find the digit that selects the menu line containing `label`.

    The launcher menu differs by disk (a REPL disk's `1 REPL` vs a compiler
    disk's `1 FILE SELECTOR`, and `RUN TESTS` sits on different numbers), so we
    read the number off the screen rather than hard-coding it."""
    up = label.upper()
    for line in screen.splitlines():
        if up in line.upper():
            for ch in line:
                if ch.isdigit():
                    return ch
    return None


# Boot to the SwiftII launcher takes ~150M cycles (ProDOS + the boot stub +
# the launcher), so the boot/transition waits need a big budget; it's all in
# izapple2 fast mode, so wall time stays small.
def wait_for(d: Headless, token: str, chunk: int = 6000, tries: int = 40) -> tuple[str, bool]:
    """Advance in `chunk`-kcycle steps until `token` appears on screen."""
    s = ""
    up = token.upper()
    for _ in range(tries):
        d.run(chunk)
        s = d.text()
        if up in s.upper():
            return s, True
    return s, False


def boot_launcher(d: Headless, sentinel: str, chunk: int = 8000, tries: int = 60) -> tuple[str, bool]:
    """Boot to the launcher, waiting for `sentinel`. The //e launchers first ask
    "Use 80 columns? [Y]/[N]" (the II+ has no 80-col card and skips it); answer
    N once so the rest of the run stays 40-column and text-scrapeable."""
    answered = False
    s = ""
    for _ in range(tries):
        d.run(chunk)
        s = d.text()
        up = s.upper()
        if not answered and "USE 80 COLUMN" in up:
            d.key(ord("N"))
            answered = True
            continue
        if sentinel.upper() in up:
            return s, True
    return s, False


# --------------------------------------------------------------------------
# Scenarios — each records checks on `rec` and may drop dumps/snapshots in `out`.
# --------------------------------------------------------------------------

class Recorder:
    def __init__(self, window=None, config: str = ""):
        self.checks: list[tuple[bool, str, str]] = []
        self._window = window      # LiveWindow to stream each check into (or None)
        self._config = config      # config name the checks belong to (for grouping)

    def check(self, ok: bool, name: str, detail: str = ""):
        ok = bool(ok)
        self.checks.append((ok, name, detail))
        print(f"    [{'PASS' if ok else 'FAIL'}] {name} {detail}")
        if self._window:
            self._window.add_log(self._config, ok, name, detail)

    @property
    def ok(self) -> bool:
        return all(c[0] for c in self.checks)


def scen_sweep(d: Headless, rec: Recorder, out: Path, family_b: bool,
               picker_key: int = 0x0D):
    # Wait for the menu line itself (not just the banner) so the whole menu is
    # drawn before we read the key off it.
    d.looking_for("launcher menu with a 'Run tests' option")
    s, ok = boot_launcher(d, "RUN TESTS")
    rec.check(ok, "boot", "launcher menu")
    (out / "menu.txt").write_text(s)
    if not ok:
        return
    key = menu_key(s, "RUN TESTS")        # the launcher's "RUN TESTS (DATA DISK)"
    rec.check(key is not None, "menu-run-tests", f"key={key}")
    if not key:
        return
    d.key(ord(key))                       # chain TESTRUN.SYSTEM
    d.looking_for("=== SWIFTII TEST RUNNER === intro screen")
    s, ok = wait_for(d, "TEST RUNNER")
    rec.check(ok, "testrun-intro")
    if not ok:
        (out / "after-select.txt").write_text(s)
        return
    # "ANY KEY = BEGIN" wants a printable key — CR (enter) does NOT satisfy it.
    d.key(32)                             # space = begin
    # Multi-tier disks (extras / compiler) then show a tier picker; single-tier
    # disks go straight to the countdown. Poll for whichever appears.
    for _ in range(15):
        d.run(4000)
        s = d.text()
        up = s.upper()
        if "SELECT TESTS" in up:
            # Start the selected tiers (all selected by default). The picker
            # accepts BOTH Return ($0D) and right-arrow ($15) — "RET/-> = RUN
            # SELECTED" — so the matrix exercises each on a different disk:
            # Return via sweep_repl (the Saturn extras-REPL picker, `sat`) and
            # right-arrow via sweep_fb (the Family B compiler pickers). picker_key
            # picks which this run uses.
            if picker_key == 0x0D:
                d.enter()
            else:
                d.key(picker_key)
            break
        if ("STARTING IN" in up or "RIGHTS RESERVED" in up
                or _last_nonblank(s).startswith(">")):
            break                         # already past the picker (single tier)

    # The sweep is one ProDOS reboot per test, so it needs a big cycle budget;
    # it's all fast mode. A Family A test ends at the REPL prompt and waits for
    # Ctrl-D (so big chunks are safe — the prompt persists); a Family B test
    # auto-advances. A clean test prints the tally "PASS n FAIL 0"; a genuine
    # failure is a "FAIL" NOT followed by " 0" (and `\b` excludes TESTRUN's
    # "FAILED" UI label). We only inspect the post-test prompt screen.
    # Family B runs all three tiers (~20 tests), each a compile + run + reboot,
    # so it needs a much bigger budget than a single-tier REPL sweep.
    d.looking_for("each test prints 'PASS n FAIL 0'; sweep ends at 'TEST RUN RESULTS'"
                  + (" + TESTLOG all P" if family_b else ""))
    seen_fail = False
    results = None
    for _ in range(700):
        d.run(20000)
        s = d.text()
        up = s.upper()
        if "TEST RUN RESULTS" in up or "RAN " in up:
            results = s
            break
        if not family_b and _last_nonblank(s).startswith(">"):
            if re.search(r"\bFAIL\b(?! 0)", s):     # a non-zero fail on this result screen
                seen_fail = True
                (out / "failure.txt").write_text(s)
            d.key(4)                      # Ctrl-D = advance to next test

    rec.check(results is not None, "sweep-reached-results")
    rec.check(not seen_fail, "no-FAIL-token-on-screen")
    if results:
        (out / "results.txt").write_text(results)
        rec.check("RAN" in results.upper(), "ran-summary")


def scen_sweep_repl(d, rec, out):
    # Return on the tier picker (the extras-REPL `sat` disk reaches it).
    scen_sweep(d, rec, out, family_b=False, picker_key=0x0D)


def scen_sweep_fb(d, rec, out):
    # Right-arrow ($15) on the tier picker (the Family B compiler disks).
    scen_sweep(d, rec, out, family_b=True, picker_key=0x15)


def scen_graphics(d: Headless, rec: Recorder, out: Path):
    d.looking_for("REPL on the extras disk")
    s, ok = boot_launcher(d, "REPL")
    rec.check(ok, "boot")
    if not ok:
        return
    d.key(ord(menu_key(s, "REPL") or "1"))
    s, ok = wait_for(d, ":HELP")          # REPL banner ("Type :help ...")
    rec.check(ok, "repl-prompt")
    d.looking_for("GR drawing: white top+left edges, magenta dot near centre")
    for line in ("gr()", "color(15)", "hlin(0, 39, 0)", "vlin(0, 39, 0)",
                 "color(1)", "plot(20, 20)"):
        d.repl_line(line)
    d.run(2000)
    d.png(out / "gr.png")
    rec.check((out / "gr.png").exists(), "gr-snapshot", str(out / "gr.png"))
    d.repl_line("text()")


def scen_videx(d: Headless, rec: Recorder, out: Path):
    d.looking_for("REPL on the Saturn disk (Videx in slot 3)")
    s, ok = boot_launcher(d, "REPL")
    rec.check(ok, "boot")
    if not ok:
        return
    d.key(ord(menu_key(s, "REPL") or "1"))
    s, ok = wait_for(d, ":HELP")
    rec.check(ok, "repl-prompt")
    d.looking_for("80-column screen after text80() (izapple2 Videx ROM diverges)")
    d.repl_line("text80()")
    d.run(2000)
    d.png(out / "videx80.png")
    rec.check((out / "videx80.png").exists(), "videx80-snapshot")
    d.repl_line("htab(70)")
    d.repl_line('print("R")')
    d.run(1500)
    d.png(out / "videx80_text.png")
    d.repl_line("text()")


def scen_keyboard(d: Headless, rec: Recorder, out: Path, iie: bool = False):
    """Walk the keyboard shortcuts in every UI mode (TESTING-keyboard.md) — the
    launcher menu, About, the Debug pager, the file selector, the REPL line
    editor + meta-commands, and the editor. These modes read the keyboard inline
    in cc65 loops (✋ "manual only by design" in the matrix), so driving them on a
    real izapple2 is exactly the coverage that doc calls its test of record."""
    # 29 stages always run + machine-specific ones: II+ adds the two Ctrl-W
    # underscore steps (31 total); //e adds history-recall, the 80-col menu
    # toggle, and the editor's two ^W width-flip steps (33 total). Keep this in
    # sync if you add/remove a stage() below.
    total = 33 if iie else 31
    n = [0]

    def stage(label):
        n[0] += 1
        d.progress(n[0], total, f"keyboard {n[0]}/{total}")
        d.looking_for(label)

    def want(name, token, tries=8, chunk=2500):       # in-mode redraw (fast)
        s, ok = wait_for(d, token, chunk=chunk, tries=tries)
        rec.check(ok, name, f"saw '{token}'" if ok else f"MISSING '{token}'")
        return s, ok

    def want_chain(name, token):                      # a SYS chain / cold reboot
        return want(name, token, tries=26, chunk=6000)

    # --- 1. Launcher main menu (TESTING-keyboard section 1) ---
    stage("launcher menu: REPL / File selector / Debug / About")
    _, ok = boot_launcher(d, "ABOUT")
    rec.check(ok, "launcher-menu", "menu drawn")
    if not ok:
        return

    stage("I / M move the > highlight")
    before = d.text(); d.key(ord("M")); d.run(800)
    moved = d.text(); d.key(ord("I")); d.run(800)
    rec.check(before != moved, "menu-nav-IM", "highlight moved")

    # Right-arrow ($15/Ctrl-U) activates the highlighted option, same as Return
    # (commit "Accept right-arrow as 'select' in launcher menu …"). About is the
    # last option, so a fistful of M presses clamps the highlight onto it; the
    # right-arrow then opens it (a digit/letter key would bypass the highlight,
    # so this is the path that actually exercises the new accept-key).
    stage("M*, right-arrow ($15) activates the highlight -> About")
    for _ in range(6):
        d.key(ord("M")); d.run(400)
    d.key(21); want("menu-rightarrow-select", "COMPILED W/")
    stage("Esc -> back to the menu")
    d.key(27); want("menu-rightarrow-Esc-back", "ABOUT")

    stage("A -> About screen (Compiled w/ cc65 …)")
    d.key(ord("A")); want("launcher-About", "COMPILED W/")
    stage("Esc -> back to the menu")
    d.key(27); want("About-Esc-back", "ABOUT")

    # --- 6. Debug pager: 3 pages, arrows, Esc ---
    stage("D -> Debug, page 1 VOLUMES")
    d.key(ord("D")); want_chain("debug-volumes", "VOLUMES")
    stage("right-arrow -> page 2 DETECTION")
    d.key(21); want("debug-detection", "DETECTION")
    # AUX RAM moved onto the DETECTION page when the MEMORY page was dropped.
    rec.check("AUX RAM" in d.text().upper(), "debug-detection-auxram",
              "AUX RAM on DETECTION page")
    stage("right-arrow -> page 3 SLOTS")
    d.key(21); want("debug-slots", "SLOTS")
    stage("left-arrow -> back to DETECTION")
    d.key(8); want("debug-prev-page", "DETECTION")
    stage("Esc -> back to the launcher")
    d.key(27); want_chain("debug-Esc-back", "ABOUT")

    # --- 2 + 3 + 4. Volume picker -> file browser -> editor ---
    stage("File selector -> DISKS / VOLUMES picker")
    d.key(ord(menu_key(d.text(), "FILE") or "2"))
    want("volume-picker", "DISKS")
    stage("volume picker: /RAM unhooked, I/M move, RET opens the boot volume")
    # /RAM-absent regression guard (ROADMAP Phase 19). On a 128K //e, ProDOS
    # installs a /RAM volume backed by the aux RAM the extras builds reuse, and
    # the boot launcher's a_unhook_ram drops it from the on-line device list
    # before this picker (and any chained DEBUG.SYSTEM) enumerates devices. II+
    # never has /RAM, so the boot volume is first and the list must omit /RAM on
    # BOTH machines. (Before the unhook the //e listed /RAM first, which is why
    # this used to need an extra step-down before opening a volume with files.)
    rec.check("/RAM" not in d.text().upper(), "volume-picker-no-ram",
              "/RAM not in the volume picker")
    d.key(ord("M")); d.run(700); d.key(ord("I")); d.run(700)
    d.enter(); want("browser", "[E]DIT")
    stage("browser: I/M highlight, J/K preview, Ctrl-T/Ctrl-V page")
    for code in (ord("M"), ord("I"), ord("J"), ord("K"), 20, 22):
        d.key(code); d.run(700)
    rec.check("[E]DIT" in d.text().upper(), "browser-nav", "still the browser")

    stage("F = new file -> editor (^S SAVE … ^Q QUIT)")
    d.key(ord("F")); want("editor-open", "^S SAVE")
    stage("type abc -> buffer shows ABC, status EDITED")
    d.type_("abc"); d.run(1200)
    s = d.text().upper()
    rec.check("ABC" in s, "editor-typing", "buffer shows ABC")
    rec.check("EDITED" in s, "editor-dirty", "status EDITED")
    stage("Ctrl-A -> column C1; Ctrl-E -> column C4")
    d.key(1); d.run(1500); rec.check("C1" in d.text().upper(), "editor-ctrl-a", "cursor at C1")
    d.key(5); d.run(1500); rec.check("C4" in d.text().upper(), "editor-ctrl-e", "cursor at C4")
    stage("left / right arrows move the cursor non-destructively")
    d.key(8); d.run(1500); rec.check("C3" in d.text().upper(), "editor-left-arrow", "<- -> C3")
    d.key(21); d.run(1500); rec.check("C4" in d.text().upper(), "editor-right-arrow", "-> -> C4")
    rec.check("ABC" in d.text().upper(), "editor-arrows-nondestructive", "arrows did not delete")
    stage("Ctrl-O up / Ctrl-L down (Apple Pascal) across two lines")
    d.enter(); d.type_("de"); d.run(1200)
    rec.check("L2" in d.text().upper(), "editor-two-lines", "cursor on line 2")
    d.key(15); d.run(1500); rec.check("L1" in d.text().upper(), "editor-ctrl-o-up", "Ctrl-O -> line 1")
    d.key(12); d.run(1500); rec.check("L2" in d.text().upper(), "editor-ctrl-l-down", "Ctrl-L -> line 2")
    if not iie:
        stage("Ctrl-W inserts _ (II+ input-method underscore, same key as the REPL)")
        d.key(23); d.run(1200)
        rec.check("_" in d.text(), "editor-ctrl-w-underscore", "Ctrl-W typed _")
    if iie:
        # //e editor: Ctrl-W toggles the text width 40<->80. The 40->80 path
        # (width80_on -> JSR $C300) used to corrupt ProDOS MLI — cc65 file I/O
        # leaves the language card banked to read RAM, so the firmware ran the
        # MLI body in LC RAM as if it were ROM; the breakage surfaced only at
        # the next MLI call, where Ctrl-Q's directory re-read drew a garbled
        # file listing (commit "launcher///e: bank ROM before JSR $C300 …").
        # Flip to 80 (running the corrupting path) and back to 40 (leaving the
        # rest of the walk unchanged); the "listing intact" check on the discard
        # step below is the regression guard that the round-trip left MLI healthy.
        #
        # Re-send Ctrl-W until the width actually flips: headless can drop the
        # first key injected just after the previous step's redraw, so one key
        # isn't reliably one toggle. We check the width BEFORE each send so a
        # registered key can't overshoot (toggle back past the target). The `text`
        # dump renders the //e 80-col page cleanly, so the box widening from ~40
        # to ~80 cols is a reliable signal that the toggle took.
        def flip_to(wide: bool, tries: int = 8) -> bool:
            for _ in range(tries):
                if (_scr_width(d.text()) > 60) == wide:
                    return True
                d.key(23); d.run(3000)
            return (_scr_width(d.text()) > 60) == wide

        stage("Ctrl-W flips editor to 80 cols (the JSR $C300 path)")
        rec.check(flip_to(True), "editor-width-to-80", "editor redrew at 80 columns")
        stage("Ctrl-W flips editor back to 40 cols")
        rec.check(flip_to(False), "editor-width-to-40", "editor redrew at 40 columns")
    stage("Ctrl-Q -> SAVE CHANGES? ; N discards")
    d.key(17); want("editor-quit-prompt", "SAVE CHANGES")
    d.key(ord("N")); _, ok = want("editor-discard", "[E]DIT")
    if iie and ok:
        # MLI healthy after the editor's 80-col round-trip => the directory
        # re-read drew a real listing, so the browser names its highlighted
        # entry. A corrupted MLI (the pre-fix bug) zeroes/garbles the panes, so
        # _highlight_name finds no entry-header line. This is the text-side
        # regression guard for the JSR $C300 fix exercised above.
        name = _highlight_name(d.text())
        rec.check(name is not None, "editor-80col-listing-intact",
                  f"browser lists '{name}' after the editor's 80-col toggle"
                  if name else "no entry listed (MLI re-read failed?)")
    stage("Q, Q -> back to the launcher menu")
    d.key(ord("Q")); d.run(1500); d.key(ord("Q")); want("file-back-menu", "ABOUT", tries=14)

    # --- 5. REPL line editor + meta-commands ---
    stage("REPL: enter (1)")
    d.key(ord(menu_key(d.text(), "REPL") or "1"))
    want_chain("repl-enter", ":HELP")

    def repl(line):
        d.type_(line); d.enter(); d.run(2500)

    stage('REPL: "1 + 2" -> 3'); repl("1 + 2"); want("repl-expr", "3")
    stage("REPL: :help -> command list"); repl(":help"); want("repl-help", "EXIT")
    stage("REPL: let x = 5 ; :list"); repl("let x = 5"); repl(":list")
    want("repl-list", "LET X")
    stage("REPL: :mem -> heap"); repl(":mem"); want("repl-mem", "HEAP")
    stage("REPL: :reset -> cleared"); repl(":reset"); want("repl-reset", "CLEARED")
    stage("REPL: backspace edits the line")
    d.type_("99"); d.key(8); d.enter(); d.run(2500)
    rec.check(True, "repl-backspace", "exercised (see window)")
    if not iie:
        stage("REPL: Ctrl-W inserts _ (II+ underscore key, same as the editor)")
        d.type_("let "); d.key(23); d.type_("v = 8"); d.run(1500)
        rec.check("_" in d.text(), "repl-ctrl-w-underscore", "Ctrl-W echoed _")
        d.enter(); d.run(2500)
    if iie:
        stage("REPL: up-arrow recalls history; down restores (//e)")
        repl("let y = 7")
        d.key(11); d.run(900)        # up-arrow: recall the older line
        d.key(10); d.run(900)        # down-arrow: back toward the in-progress line
        rec.check(True, "repl-history-iie", "exercised (see window)")
        d.enter(); d.run(2000)       # clear the line so Ctrl-D sees it empty
    stage("REPL: Ctrl-D on empty line -> exit to launcher")
    d.key(4); want_chain("repl-ctrl-d-exit", "ABOUT")

    if iie:
        stage("//e: W toggles the menu between 40 and 80 columns")
        d.key(27); wait_for(d, "ABOUT", chunk=4000, tries=12)
        before = d.text(); d.key(ord("W")); d.run(1500)
        rec.check(before != d.text(), "menu-80col-W", "W toggled the column width")
        d.key(ord("W")); d.run(1500)   # toggle back to 40


def scen_keyboard_iie(d, rec, out):
    scen_keyboard(d, rec, out, iie=True)


# The digraph scenario saves its scratch file to the data-disk root by absolute
# path, so the editor's save lands on the writable drive-2 copy (drive 1 is the
# read-only program disk) where AppleCommander can read the bytes back.
DG_FILE = "DGTEST.SWIFT"
DG_SAVE_PATH = "/SWIFTII.DATA/" + DG_FILE

# Two source lines typed into the editor. They cover every C-digraph and both
# case markers; what each becomes on screen / on disk is asserted below. (The
# `_` in line 1 is typed with Ctrl-W, not literally — see the type sequence.)
#   line 1 typed  "let n[Ctrl-W]x = <:1:>"   buffer "let n_x = <:1:>"
#   line 2 typed  "'a''bc <% %> ??/ ??!"     buffer (literal, letters lowercased)
# On a cooked save input_translate canonicalises both (verified on the host in
# tests/editor/fileio_test.c + by this scenario's read-back):
DG_L1_CANON = "let n_x = [1]"          # <: :> -> [ ]   ; Ctrl-W -> _
DG_L2_CANON = "ABC { } \\ |"           # ' '' case markers ; <% %> ??/ ??! -> { } \ |


def _screen_has(d: Headless, *needles: str) -> tuple[bool, str]:
    """True iff every needle is somewhere on the (flattened) screen. Returns
    (ok, flat) so the caller can log what was actually on screen on a miss."""
    flat = _flat(d.text()).upper()
    return all(n.upper() in flat for n in needles), flat


def _screen_lacks(d: Headless, *needles: str) -> tuple[bool, str]:
    flat = _flat(d.text()).upper()
    return all(n.upper() not in flat for n in needles), flat


def scen_digraphs(d: Headless, rec: Recorder, out: Path):
    """Type every digraph + case marker through the real II+ editor and verify
    the whole chain: on-screen rendering as typed, after a delete, the canonical
    bytes the ProDOS save writes (read back off the disk), and the rendering
    after a reload. The //+ input layer keeps the typed digraph/marker bytes
    *literal* in the buffer (`<%`, `'`) and only canonicalises at save, so what
    you see while typing is the digraph form; after a save+reload the buffer
    holds the canonical byte, which the editor's pre-IIe display re-expands
    (`{`->`<%`, `}`->`%>`, `|`->`??!`) while `[ ] \\` render with their native
    glyph. That before/after asymmetry is the heart of what's checked."""
    # --- boot to the data disk's file browser, open a fresh editor buffer ---
    d.looking_for("launcher menu with a 'File selector' option")
    s, ok = boot_launcher(d, "FILE SELECTOR")
    rec.check(ok, "boot", "launcher menu")
    if not ok:
        return
    d.key(ord(menu_key(s, "FILE") or "2"))     # -> volume picker
    d.looking_for("volume picker; open the SWIFTII.DATA data disk")
    _, ok = wait_for(d, "DISKS")
    rec.check(ok, "volume-picker")
    if not ok:
        return
    rec.check(pick_volume(d, "SWIFTII.DATA"), "open-data-volume", "/SWIFTII.DATA")
    _, ok = wait_for(d, "[E]DIT")              # browser legend
    rec.check(ok, "browser-open")
    if not ok:
        return

    d.looking_for("F = new file -> empty editor buffer (cooked/.swift mode)")
    d.key(ord("F"))
    _, ok = wait_for(d, "^S SAVE")
    rec.check(ok, "editor-open", "new scratch buffer")
    if not ok:
        return

    # --- type line 1: a digraph pair + the Ctrl-W underscore ---
    d.looking_for("type 'let n_x = <:1:>' (<: :> stay literal while typing)")
    d.type_("let n"); d.key(23); d.type_("x = ")   # Ctrl-W ($17) inserts _
    d.type_("<:1:>"); d.run(2000)
    d.enter(); d.run(1500)

    # --- type line 2: both case markers + the remaining digraphs ---
    d.looking_for("type \"'a''bc <% %> ??/ ??!\" — all on screen as the digraph form")
    for chunk in ("'a''bc ", "<% %> ", "??/ ", "??!"):
        d.type_(chunk); d.run(1800)

    # (1) RENDERED CORRECTLY AFTER ADDING — every digraph shows in its typed
    # (literal) form, the underscore is in line 1, and the case-marked letters
    # render (lowercase letters draw as normal-video caps; markers stay literal).
    ok, flat = _screen_has(d, "N_X", "<:", ":>", "<%", "%>", "??/", "??!", "'A", "BC")
    rec.check(ok, "render-after-adding",
              "all digraphs + markers on screen" if ok
              else f"missing some; screen: {flat[:120]!r}")
    (out / "after-typing.txt").write_text(d.text())

    # (2) RENDERED CORRECTLY AFTER A DIGRAPH IS DELETED — the cursor sits just
    # past the final `??!` (3 literal bytes); 3 backspaces remove it and the
    # screen must drop `??!` while the earlier `??/` survives. The editor's
    # destructive backspace is Ctrl-D ($04) — $08 is its NON-destructive left
    # arrow (see src/editor/keymap.h), so deleting needs key 4, not 8.
    d.looking_for("Ctrl-D x3 deletes the trailing ??! ; ??/ stays")
    for _ in range(3):
        d.key(4); d.run(700)
    gone, flat = _screen_lacks(d, "??!")
    still, _ = _screen_has(d, "??/")
    rec.check(gone and still, "render-after-delete",
              "??! removed, ??/ kept" if gone and still
              else f"delete not clean; screen: {flat[:120]!r}")
    # restore it so the saved file carries the full digraph set
    d.type_("??!"); d.run(1800)
    back, _ = _screen_has(d, "??!")
    rec.check(back, "render-after-retype", "??! re-added")

    # --- save to the data disk by absolute path (drive-2 copy is writable) ---
    d.looking_for("Ctrl-S -> SAVE AS: ; type the data-disk path")
    d.key(19)                                  # Ctrl-S
    _, ok = wait_for(d, "SAVE AS")
    rec.check(ok, "save-prompt", "SAVE AS: shown")
    if not ok:
        return
    d.type_(DG_SAVE_PATH); d.run(1500)
    d.enter()
    _, ok = wait_for(d, "SAVED", tries=20, chunk=5000)
    rec.check(ok, "save-done", f"saved to {DG_SAVE_PATH}")
    (out / "after-save.txt").write_text(d.text())

    # --- reopen the saved file and check the post-reload rendering ---
    d.looking_for("Ctrl-Q quits the editor (clean — already saved)")
    d.key(17)                                  # Ctrl-Q; no prompt (not dirty)
    _, ok = wait_for(d, "[E]DIT", tries=20, chunk=5000)
    rec.check(ok, "back-to-browser", "browser after quit")
    if not ok:
        return
    d.looking_for(f"open {DG_FILE} again from the browser")
    if not browse_to(d, DG_FILE.split(".")[0]):
        # the browser shows names without the .swift extension truncated; fall
        # back to the full base if the short form isn't matched.
        browse_to(d, DG_FILE)
    d.enter()
    _, ok = wait_for(d, "^S SAVE", tries=24, chunk=5000)
    rec.check(ok, "reopen", f"{DG_FILE} reopened in the editor")
    if not ok:
        return
    d.run(2000)
    (out / "after-reopen.txt").write_text(d.text())

    # (4) RENDERED CORRECTLY AFTER OPENING — the canonical bytes now drive the
    # display: `{ } |` re-expand to `<% %> ??!`, while `[ ] \` show with their
    # native glyph (so the brace-digraph `<:` is GONE, replaced by `[1]`, and
    # `??/` is GONE, replaced by a literal `\`). This proves the save wrote
    # canonical bytes AND the loader/display round-trips them.
    shows, flat = _screen_has(d, "[1]", "<%", "%>", "??!", "\\")
    rec.check(shows, "render-after-opening",
              "{ } | re-expand; [ ] \\ native" if shows
              else f"reload render wrong; screen: {flat[:120]!r}")
    collapsed, flat = _screen_lacks(d, "<:", "??/")
    rec.check(collapsed, "render-after-opening-collapsed",
              "no raw <: or ??/ after reload" if collapsed
              else f"stale digraph source on screen: {flat[:120]!r}")


# The ERRTESTS demos (datadisk/tests/errtests/) keyed by their on-disk base name
# (upper-cased, no .SWIFT). Each value is (expected error banner, a distinctive
# uppercase fragment of the message, or None for the runtime demos — the Runner
# prints only "runtime error", no per-trap text). The II+ renders the
# Compiler/Runner's lowercase output as normal-video uppercase, so the message
# text scrapes back as upper case; messages quote tokens with single quotes
# (not backticks, which have no pre-IIe glyph), so they scrape cleanly too.
ERR_EXPECT = {
    "EBOUNDS":  ("RUNTIME ERROR", None),               # array index out of bounds
    "EFLOW":    ("COMPILE ERROR", "BREAK OUTSIDE"),
    "EFUNCS":   ("COMPILE ERROR", "MISSING RETURN"),
    "ELEX":     ("COMPILE ERROR", "INT RANGE"),         # integer literal out of Int range
    "ENAMELEN": ("COMPILE ERROR", "NAME >11 CHARS"),
    "ENAMES":   ("COMPILE ERROR", "UNDECLARED NAME"),
    "ERUNTIME": ("RUNTIME ERROR", None),               # division by zero
    "ESTRINGS": ("COMPILE ERROR", "ESCAPE SEQUENCE"),
    "ESYNTAX":  ("COMPILE ERROR", "WANT ')'"),
}


def _unbox(line: str) -> str:
    """headless `text` wraps the 40-col screen in a '#' border ('# ' + the
    screen line + ' #'); strip it so column-anchored (leading-space-sensitive)
    matches see the raw screen line."""
    if line.startswith("# "):
        line = line[2:]
    elif line.startswith("#"):
        line = line[1:]
    if line.endswith("#"):
        line = line[:-1]
    return line.rstrip()


def _scr_width(screen: str) -> int:
    """The widest unboxed screen row — about 40 in 40-column mode and about 80 in
    80-column mode (the //e firmware 80-col page IS dumped cleanly by `text`, only
    `png` can't render it). Used to confirm the editor's Ctrl-W actually flipped
    the text width, since the JSR $C300 path only runs on the 40->80 transition."""
    return max((len(_unbox(l)) for l in screen.splitlines()), default=0)


def _highlight_name(screen: str) -> str | None:
    """The file browser's preview-header line names the highlighted entry as
    'NAME  TXT  NN B' (a file) or 'NAME  <folder>'. Return NAME (upper-case),
    or None. The list rows are indented (a marker + name) so they don't match;
    the header is the only line that starts with a bare name + two spaces."""
    for line in screen.splitlines():
        m = re.match(r"([A-Z0-9.]+)  (?:TXT|SYS|BIN|BAS|DIR|<|\$)", _unbox(line))
        if m:
            return m.group(1)
    return None


def _vol_rows(screen: str) -> list[tuple[bool, str]]:
    """Parse the volume picker's '> /NAME' (selected) / '  /NAME' rows into
    (selected, NAME) in screen order."""
    out = []
    for line in screen.splitlines():
        m = re.match(r"(> |  )/([A-Z0-9.]+)", _unbox(line))
        if m:
            out.append((m.group(1) == "> ", m.group(2)))
    return out


def pick_volume(d: Headless, volname: str, max_steps: int = 24) -> bool:
    """In the launcher volume picker, step the '>' marker (I/M) onto `volname`
    and open it (RET). One step per iteration so the marker move is read back
    before the next."""
    for _ in range(max_steps):
        rows = _vol_rows(d.text())
        names = [n for _, n in rows]
        if volname not in names:
            d.run(2000)                  # still drawing / disk settling
            continue
        sel = next((i for i, (s, _) in enumerate(rows) if s), 0)
        tgt = names.index(volname)
        if tgt == sel:
            d.enter()
            return True
        d.key(ord("M") if tgt > sel else ord("I"))
        d.run(500)
    return False


def browse_to(d: Headless, name: str, max_steps: int = 40) -> bool:
    """Move the browser highlight down (M) until the highlighted entry is
    `name`. Entering a directory resets the highlight to the top, so the
    targets are always at or below the start — only M is needed."""
    for _ in range(max_steps):
        if _highlight_name(d.text()) == name:
            return True
        d.key(ord("M"))
        d.run(600)
    return _highlight_name(d.text()) == name


def _flat(screen: str) -> str:
    """Join the unboxed 40-col rows with no separator so a message word-wrapped
    across two rows ('…STRING E' / 'SCAPE SEQUENCE') reads back whole."""
    return "".join(_unbox(l) for l in screen.splitlines())


def _ran_file(screen: str) -> str | None:
    """The Compiler/Runner error screen echoes the full path it acted on
    ('COMPILING: /…/ESTRINGS.SWIFT' / 'RUNNING: /…/EBOUNDS.SWB'); pull the base
    name (no extension) out of it. _flat() rejoins 40-col wraps and WITH_INVERSE_JM
    keeps upper-case 'J'/'M' from garbling, so the name extracts cleanly —
    scen_errtests asserts it as a regression guard for that fix (identity still
    comes from browser order)."""
    m = re.search(r"([A-Z0-9]+)\.SW", _flat(screen).upper())
    return m.group(1) if m else None


def open_errtests(d: Headless, rec: Recorder) -> bool:
    """Boot to the launcher, open the file browser, and descend to the data
    disk's TESTS/ERRTESTS/. Shared by both errtests variants (REPL + Family B);
    the launcher menu carries a 'File selector' entry on every disk. Returns
    True once the browser is sitting in ERRTESTS/."""
    d.looking_for("launcher menu with a 'File selector' option")
    s, ok = boot_launcher(d, "FILE SELECTOR")
    rec.check(ok, "boot", "launcher menu")
    if not ok:
        return False
    key = menu_key(s, "FILE")
    rec.check(key is not None, "menu-file-selector", f"key={key}")
    if not key:
        return False
    d.key(ord(key))                       # -> volume picker

    d.looking_for("volume picker; open the SWIFTII.DATA data disk")
    _, ok = wait_for(d, "DISKS")          # "Disks / volumes" header
    rec.check(ok, "volume-picker")
    if not ok:
        return False
    ok = pick_volume(d, "SWIFTII.DATA")
    rec.check(ok, "open-data-volume", "/SWIFTII.DATA")
    if not ok:
        return False

    d.looking_for("file browser; descend into TESTS/ERRTESTS/")
    _, ok = wait_for(d, "[E]DIT")         # browser legend
    rec.check(ok, "browser-open")
    if not ok:
        return False
    for sub in ("TESTS", "ERRTESTS"):
        if not browse_to(d, sub):
            rec.check(False, f"enter-{sub}", f"{sub}/ not found in browser")
            return False
        d.enter()                         # RET enters the folder
        d.run(1500)
    rec.check(True, "reached-errtests", "TESTS/ERRTESTS/")
    return True


def scen_errtests(d: Headless, rec: Recorder, out: Path):
    """Run the deliberately-failing ERRTESTS demos on a Family B compiler disk
    and verify each triggers the on-target error display the host suite can't
    reach. The TESTRUN.SYSTEM 'Run tests' sweep skips these (they don't self-check to
    'fail 0'), so this drives them by hand: open the file browser, descend to
    the data disk's TESTS/ERRTESTS/, and press [X] on each. The launcher's
    LASTRUN resume reopens the browser on the just-run file after the error's
    'press any key' pause, so we step to the next with M."""
    if not open_errtests(d, rec):
        return

    # One [X] per file. [X] runs the highlighted .swift; the LASTRUN resume
    # lands the highlight back on it, so M steps to the next. We identify each
    # demo by its alphabetical browser order (the disk order) — the same anchor
    # the REPL variant uses — and cross-check it with both the per-file message
    # fragment and the path echo. On a pre-IIe (II+) screen upper-case renders
    # via the inverse-video path, whose screen codes for 'J' ($0A) and 'M' ($0D)
    # USED to collide with cputc's LF/CR and garble any name containing them
    # (ENAMELEN / ENAMES / ERUNTIME). WITH_INVERSE_JM now fixes that on the
    # Runner AND the II+ Compiler, so the echo names every file cleanly and is
    # asserted as a regression guard below (see the per-demo echo check).
    order = sorted(ERR_EXPECT)            # the browser lists them alphabetically
    n_files = len(order)
    seen: dict[str, bool] = {}
    for i, base in enumerate(order):
        kind, frag = ERR_EXPECT[base]
        d.progress(i + 1, n_files, f"errtest {i + 1}/{n_files}")
        d.looking_for(f"{base}: on-target {kind.lower()}")
        d.key(ord("X"))                   # compile (+ run) the highlighted .swift
        # The error path ends at "Press any key to continue..."; the screen then
        # also carries the COMPILE/RUNTIME ERROR banner + (compile) the message.
        es, got = wait_for(d, "PRESS ANY KEY", chunk=8000, tries=40)
        flat = _flat(es).upper()
        (out / f"{base}.txt").write_text(es)

        # Path echo cross-check — a regression guard for the J/M render fix on
        # BOTH paths. RUNTIME demos echo 'RUNNING:' via the Runner and COMPILE
        # demos echo 'COMPILING:' via the II+ Compiler; both binaries carry
        # WITH_INVERSE_JM, so an upper-case 'J'/'M' renders cleanly instead of
        # garbling. ENAMELEN / ENAMES (compile) and ERUNTIME (runtime) all carry
        # an 'M' that used to wrap the line, so this asserts the fix holds. (The
        # //e Compiler, not exercised here, renders the same names via WITH_IIE.)
        echoed = _ran_file(es)
        rec.check(echoed == base, f"errtest-{base}-echo",
                  f"echo named {echoed!r}" if echoed == base
                  else f"expected {base}, echo named {echoed!r}")

        banner_ok = got and (kind in flat)
        rec.check(banner_ok, f"errtest-{base}",
                  f"saw {kind}" if banner_ok
                  else f"MISSING {kind}; last: {_last_nonblank(es)!r}")
        if frag:
            rec.check(frag in flat, f"errtest-{base}-msg",
                      f"'{frag}'" if frag in flat else f"MISSING '{frag}'")
        seen[base] = True

        # Dismiss the pause; the launcher chains back and resumes the browser on
        # this file. Step down to the next one (skip after the last).
        d.key(32)                         # any key -> back to the launcher/browser
        _, rok = wait_for(d, "[E]DIT", chunk=8000, tries=40)
        rec.check(rok, f"resume-after-{base}", "browser reopened")
        if not rok:
            return
        if i < n_files - 1:
            d.key(ord("M"))
            d.run(700)

    rec.check(set(seen) == set(ERR_EXPECT), "errtests-all-visited",
              f"{len(seen)}/{n_files}: {sorted(seen)}")


def scen_errtests_repl(d: Headless, rec: Recorder, out: Path):
    """The REPL (Family A) counterpart of scen_errtests: run the same demos via
    [X] on a *REPL* disk, exercising the interpreter's own error display
    (repl.c run_source) — a separate code path from the Family B Compiler/Runner.
    It differs in three ways the driving has to honour:
      * framing — 'compile error: <msg>' with NO 'line N:' prefix, and
        'runtime error'; the message text is the same, so the fragment checks
        still apply;
      * no pause — after the error it drops straight to the '> ' prompt (no
        'press any key'), so we settle on the prompt and advance with Ctrl-D
        (exit REPL -> cold reboot -> launcher resumes the browser via LASTRUN);
      * no path echo — the REPL screen doesn't print the file it ran, so we
        identify each demo by its alphabetical browser order (the disk order)
        and let the per-file message fragment cross-check it."""
    if not open_errtests(d, rec):
        return

    order = sorted(ERR_EXPECT)            # the browser lists them alphabetically
    n = len(order)
    for i, base in enumerate(order):
        kind, frag = ERR_EXPECT[base]
        d.progress(i + 1, n, f"errtest {i + 1}/{n}")
        d.looking_for(f"{base}: REPL {kind.lower()} display")
        d.key(ord("X"))                   # run the staged .swift on the REPL
        # run_source prints the error then the REPL loop redraws '> ' — so once
        # the prompt is back, the error line is already on screen.
        es = ""
        for _ in range(40):
            d.run(8000)
            es = d.text()
            if _last_nonblank(es).startswith(">"):
                break
        flat = _flat(es).upper()
        (out / f"{base}.txt").write_text(es)
        banner_ok = kind in flat
        rec.check(banner_ok, f"errtest-{base}",
                  f"saw {kind}" if banner_ok
                  else f"MISSING {kind}; last: {_last_nonblank(es)!r}")
        if frag:
            rec.check(frag in flat, f"errtest-{base}-msg",
                      f"'{frag}'" if frag in flat else f"MISSING '{frag}'")

        # Ctrl-D on the empty prompt exits the REPL; the launcher resumes the
        # browser on this file (LASTRUN). Step to the next with M.
        d.key(4)
        _, rok = wait_for(d, "[E]DIT", chunk=8000, tries=40)
        rec.check(rok, f"resume-after-{base}", "browser reopened")
        if not rok:
            return
        if i < n - 1:
            d.key(ord("M"))
            d.run(700)

    rec.check(True, "errtests-repl-complete", f"{n} demos")


def _marked_swift(screen: str):
    """Name on the file browser's '>'-marked row (the highlighted .swift)."""
    for ln in screen.splitlines():
        m = re.search(r">\s+([A-Z0-9.]+\.SWIFT)", _unbox(ln))
        if m:
            return m.group(1)
    return None


def _select_in_dir(d: Headless, name: str) -> bool:
    """Within the current browser directory, move the highlight onto `name`.
    Go to the top first (I) so it works regardless of a LASTRUN resume position,
    then step down (M). Confirm via BOTH the marked row and the preview footer
    (which lags a frame), and re-read after a further settle so a lagging frame
    can't false-match mid-move. Settles up front because keys sent before the
    browser finishes (re)drawing are dropped."""
    d.run(2500)                              # settle (post-resume keys drop)
    for _ in range(16):
        d.key(ord("I")); d.run(400)          # to the top of the list
    for _ in range(24):
        d.run(1200)
        if _marked_swift(d.text()) == name and _highlight_name(d.text()) == name:
            d.run(700)                        # let the frame settle, then re-read
            if _marked_swift(d.text()) == name and _highlight_name(d.text()) == name:
                return True
        d.key(ord("M"))                       # step down
    return False


def scen_samples(d: Headless, rec: Recorder, out: Path, flat: bool):
    """[X]-run the data disk's oversize showcases on a Family B compiler disk and
    assert each prints its checksum (or, for xfuncs on the flat II+ tier, that it
    is rejected by design). The TESTRUN 'Run tests' sweep covers TESTS/, not the
    user-facing SAMPLES/XSAMPLES — this is the only automated proof that the
    shipped showcases actually compile+run on the tier each disk targets.

      xbig    -> checksum = 6265   (all three tiers, since it was function-wrapped)
      xgrdemo -> checksum = 1552   (all three tiers)
      xfuncs  -> 210 on the paged tiers; on flat II+ the total bytecode exceeds
                 the whole-program buffer, so the Compiler rejects it by design."""
    # (sample, expected-substring, must-it-error?) for this tier.
    plan = [("XBIG.SWIFT",    "6265", False),
            ("XGRDEMO.SWIFT", "1552", False),
            ("XFUNCS.SWIFT",  ("TOO BIG" if flat else "210"), flat)]

    d.looking_for("launcher menu with a 'File selector' option")
    s, ok = boot_launcher(d, "FILE SELECTOR")
    rec.check(ok, "boot", "launcher menu")
    if not ok:
        return
    d.key(ord(menu_key(s, "FILE") or "3"))
    _, ok = wait_for(d, "DISKS")
    rec.check(ok, "volume-picker")
    if not ok:
        return
    rec.check(pick_volume(d, "SWIFTII.DATA"), "open-data-volume")
    _, ok = wait_for(d, "[E]DIT")
    rec.check(ok, "browser-open")
    if not ok:
        return
    d.run(3000)                              # settle (else the first keys drop)
    if not browse_to(d, "XSAMPLES"):
        rec.check(False, "enter-XSAMPLES", "XSAMPLES/ not found")
        return
    d.run(1500); d.enter(); d.run(4000)      # descend into XSAMPLES/

    n = len(plan)
    for i, (fname, want, must_err) in enumerate(plan):
        base = fname.split(".")[0].lower()
        d.progress(i + 1, n, f"sample {i + 1}/{n}")
        d.looking_for(f"{base}: {'rejected by design' if must_err else want}")
        if not _select_in_dir(d, fname):
            rec.check(False, f"select-{base}",
                      f"could not land on {fname} (marked {_marked_swift(d.text())!r})")
            continue
        d.key(ord("X"))                      # compile (+ run) the highlighted .swift
        res = None
        for _ in range(80):                  # Family B: compile, chain Runner, run
            d.run(20000)
            up = d.text().upper()
            if any(t in up for t in ("CHECKSUM", "= 210", "TOO BIG", "EXCEEDS",
                                     "ERROR", "PRESS ANY KEY")):
                res = d.text(); break
        flat_scr = _flat(d.text()).upper()
        (out / f"sample-{base}.txt").write_text(d.text())
        if must_err:
            got = ("TOO BIG" in flat_scr or "EXCEEDS" in flat_scr
                   or "ERROR" in flat_scr)
            rec.check(got, f"sample-{base}-rejected",
                      "rejected" if got else f"expected rejection; {_last_nonblank(d.text())!r}")
        else:
            got = want in flat_scr
            rec.check(got, f"sample-{base}",
                      f"{want}" if got else f"want {want}; {_last_nonblank(d.text())!r}")
        # Dismiss the pause / error and let the launcher resume the browser
        # (back in XSAMPLES/ on the just-run file) before the next sample.
        d.key(32)
        wait_for(d, "[E]DIT", chunk=8000, tries=40)


def scen_samples_flat(d, rec, out):
    scen_samples(d, rec, out, flat=True)


def scen_samples_paged(d, rec, out):
    scen_samples(d, rec, out, flat=False)


SCENARIOS = {
    "sweep_repl": scen_sweep_repl,
    "sweep_fb": scen_sweep_fb,
    "graphics": scen_graphics,
    "videx": scen_videx,
    "keyboard": scen_keyboard,
    "keyboard_iie": scen_keyboard_iie,
    "digraphs": scen_digraphs,
    "errtests": scen_errtests,
    "errtests_repl": scen_errtests_repl,
    "samples_flat": scen_samples_flat,
    "samples_paged": scen_samples_paged,
}


# --------------------------------------------------------------------------
# Orchestration
# --------------------------------------------------------------------------

def sh(cmd: list[str], dry: bool) -> int:
    print("  $ " + " ".join(str(c) for c in cmd))
    return 0 if dry else subprocess.call(cmd)


def build_disks(cfg: Config, dry: bool):
    targets = [cfg.target] + (["disk-data"] if cfg.data else [])
    sh(["make", "-C", str(ROOT), *targets], dry)


def find_java() -> str:
    """A real JDK. macOS's bare `java` is a stub that errors if no JDK is on
    PATH, so prefer JAVA_HOME / java_home / a Homebrew openjdk before it."""
    import glob
    cands: list[str] = []
    if os.environ.get("JAVA_HOME"):
        cands.append(str(Path(os.environ["JAVA_HOME"]) / "bin" / "java"))
    try:
        h = subprocess.check_output(["/usr/libexec/java_home"], text=True,
                                    stderr=subprocess.DEVNULL).strip()
        cands.append(str(Path(h) / "bin" / "java"))
    except Exception:
        pass
    cands += sorted(glob.glob("/opt/homebrew/opt/openjdk*/bin/java"))
    cands += sorted(glob.glob("/usr/local/opt/openjdk*/bin/java"))
    for c in cands:
        if Path(c).exists():
            return c
    return shutil.which("java") or "java"   # Linux/CI: a real `java` is on PATH


def read_disk_file(image: Path, name: str, out: Path) -> str | None:
    """Extract `name` from a ProDOS .po with AppleCommander (`-g`) and return its
    text. Used for the Family B TESTLOG and the digraph scenario's saved file —
    both need to inspect bytes the emulator wrote, which only a real read-back
    off the disk image can confirm. None if AppleCommander/the image is missing
    or the file isn't there."""
    if not AC_JAR.exists() or not image.exists():
        return None
    try:
        subprocess.check_call(
            [find_java(), "-jar", str(AC_JAR), "-g", str(image), name, str(out)],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    except subprocess.CalledProcessError:
        return None
    return out.read_bytes().decode("ascii", "replace").strip() if out.exists() else None


def read_testlog(image: Path, out: Path) -> str | None:
    return read_disk_file(image, "TESTLOG", out)


def run_config(cfg: Config, binary: str, workdir: Path, dry: bool,
               show: bool = False, window=None,
               disk_dir: Path = DISK_DIR, build: bool = True) -> dict:
    print(f"\n=== {cfg.name}  ({cfg.flags} / {cfg.scenario}) ===")
    if build:
        build_disks(cfg, dry)          # fresh-built disks; pre-built dirs skip this

    out = workdir / cfg.name
    out.mkdir(parents=True, exist_ok=True)

    # Both disks are mounted read/write and write scenarios (editor saves, file
    # CRUD) modify them, so always run off per-config COPIES in the workdir — the
    # source images are never touched. This matters most with --disk-dir, where
    # they may be a tagged release we must leave pristine.
    src_disk1 = _resolve_disk(disk_dir, cfg.disk)
    if not dry and not src_disk1.exists():
        msg = f"disk image not found: {src_disk1}"
        print("  " + msg, file=sys.stderr)
        return {"config": cfg.name, "verdict": "fail",
                "checks": [(False, "disk-missing", msg)],
                "fb_pass": None, "fb_fail": None}
    disk1 = out / cfg.disk
    if not dry:
        shutil.copy2(src_disk1, disk1)
    diskii = f"diskii,disk1={disk1}"
    data_po = _resolve_disk(disk_dir, "swiftii-data.po")
    data_copy = None
    if cfg.data:
        data_copy = out / "data.po"
        if not dry and data_po.exists():
            shutil.copy2(data_po, data_copy)
        diskii += f",disk2={data_copy}"
    args = [*FLAGS[cfg.flags], "-s6", diskii]

    if dry:
        print("  $ " + binary + " " + " ".join(args))
        return {"config": cfg.name, "verdict": "dry-run", "checks": [],
                "fb_pass": None, "fb_fail": None}

    if window:
        window.set_config(f"{cfg.name}  ·  {scenario_label(cfg.scenario)}",
                          HW_SPECS.get(cfg.flags, cfg.flags), scenario_desc(cfg.scenario))

    rec = Recorder(window=window, config=cfg.name)
    error = None
    try:
        d = Headless(binary, args, out, show=show, window=window)
        if window:
            window.attach_headless(d)     # so the page's Stop button can kill it
        try:
            SCENARIOS[cfg.scenario](d, rec, out)
        finally:
            d.close()
    except Exception as e:                # noqa: BLE001 — one bad config shouldn't stop the matrix
        error = str(e)
        rec.check(False, "driver-error", error)

    fb_pass = fb_fail = None
    if cfg.scenario == "sweep_fb" and data_copy:
        tl = read_testlog(data_copy, out / "TESTLOG")
        if tl is not None:
            fb_pass, fb_fail = tl.count("P"), tl.count("F")
            rec.check(fb_fail == 0, "testlog", f"{fb_pass} pass, {fb_fail} fail")

    # (3) SAVED CORRECTLY — read the digraph scenario's scratch file back off the
    # data disk and assert the editor wrote canonical bytes: every digraph
    # collapsed to its target glyph and the case markers produced real capitals,
    # with NO raw digraph source (`<%`, `??/`, …) left behind. This is the
    # ground-truth check the on-screen scrape can't give (inverse-vs-normal video
    # is indistinguishable in a text dump), so case correctness rides on it.
    if cfg.scenario == "digraphs" and data_copy and not error:
        body = read_disk_file(data_copy, DG_FILE, out / DG_FILE)
        if body is None:
            rec.check(False, "saved-file-readback",
                      f"could not read {DG_FILE} (AppleCommander/Java present?)")
        else:
            # Space-agnostic token check (the headless `type` verb strips a
            # chunk's trailing space, so `= <:1:>` lands as `=[1]` — the inter-
            # token spacing is a typing artefact, not a canonicalisation one).
            # `n_x` proves Ctrl-W's `_` + lowercase passthrough; `[1]` proves
            # `<: :>`; `ABC` (capitals, not `abc`) proves the ' / '' case
            # markers; `{ } \ |` prove `<% %> ??/ ??!`.
            want = ("n_x", "[1]", "ABC", "{", "}", "\\", "|")
            miss = [w for w in want if w not in body]
            rec.check(not miss, "saved-file-canonical",
                      "all canonical tokens present" if not miss
                      else f"missing {miss!r}; got {body!r}")
            leaked = [g for g in ("<%", "%>", "<:", ":>", "??/", "??!")
                      if g in body]
            rec.check(not leaked, "saved-file-no-raw-digraphs",
                      "no raw digraph source on disk" if not leaked
                      else f"raw digraph(s) {leaked!r} in {body!r}")

    verdict = "pass" if rec.ok and not error else "fail"
    return {"config": cfg.name, "verdict": verdict, "checks": rec.checks,
            "fb_pass": fb_pass, "fb_fail": fb_fail}


def run_interactive(window: "LiveWindow", binary: str, workdir: Path,
                    show: bool, disk_dir: Path = DISK_DIR,
                    build: bool = True) -> list[dict]:
    """Queue-driven run loop for the live window. Idle until the user presses
    Start, then pull the next queued config and run it; the page can stop the
    current one, stop them all, or tick/untick configs to change what's left.
    Runs until the user interrupts (Ctrl-C); the report prints whatever ran."""
    results: list[dict] = []
    try:
        while True:
            name = window.next_config()
            if name is None:
                time.sleep(0.2)              # idle: wait for Start / more ticks
                continue
            t0 = time.time()
            r = run_config(BY_NAME[name], binary, workdir, False, show, window,
                           disk_dir=disk_dir, build=build)
            if window.consume_stop(name):    # user halted this one mid-run
                r["verdict"] = "stopped"
            window.finish_config(name, r["verdict"], time.time() - t0)
            results = [x for x in results if x["config"] != name] + [r]   # re-runs replace
    except KeyboardInterrupt:
        print("\nacceptance: interrupted — stopping.")
    return results


def _fmt_dur(secs: float) -> str:
    m, s = divmod(int(round(secs)), 60)
    return f"{m}m {s:02d}s" if m else f"{s}s"


def print_report(results: list[dict], elapsed: float | None = None) -> int:
    print("\n" + "=" * 60 + "\nACCEPTANCE SUMMARY\n" + "=" * 60)
    worst = 0
    for r in results:
        tag = {"pass": "PASS", "fail": "FAIL", "dry-run": "DRY ",
               "stopped": "STOP"}.get(r["verdict"], "????")
        extra = ""
        if r["fb_pass"] is not None:
            extra = f"  TESTLOG: {r['fb_pass']} pass, {r['fb_fail']} fail"
        if r["verdict"] == "stopped":
            extra = "  (stopped by user)"
        print(f"  [{tag}] {r['config']:<14}{extra}")
        if r["verdict"] != "stopped":       # an interrupted run's checks are noise
            for ok, name, detail in r["checks"]:
                if not ok:
                    print(f"          - FAIL {name} {detail}")
        if r["verdict"] == "fail":
            worst = 1
    if elapsed is not None:
        print(f"  elapsed: {_fmt_dur(elapsed)}")
    print("=" * 60)
    return worst


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("configs", nargs="*", help="config names (default: all)")
    ap.add_argument("--list", action="store_true", help="list configs and exit")
    ap.add_argument("--dry-run", action="store_true", help="print the plan, run nothing")
    ap.add_argument("--show", action="store_true",
                    help="echo izapple2's screen to the terminal live as it runs "
                         "(eyeball the tests; redraws in place per screen change)")
    ap.add_argument("--window", action="store_true",
                    help="open a GUI window (browser) mirroring izapple2's rendered "
                         "screen live, incl. graphics; keeps the terminal clean")
    ap.add_argument("--workdir", default=None)
    ap.add_argument("--disk-dir", default=None,
                    help="run against pre-built disk images in this directory "
                         "(e.g. releases/v1.0.1) instead of building fresh from "
                         "source; the make targets are skipped. The images are "
                         "copied into the workdir per config and run from there, "
                         "so the originals are never modified")
    args = ap.parse_args()

    if args.list:
        for c in CONFIGS:
            tail = "drive2:data" if c.data else "no-data"
            print(f"  {c.name:<14} {c.flags:<7} {c.scenario:<11} {tail}")
        return 0

    selected = CONFIGS
    if args.configs:
        bad = [n for n in args.configs if n not in BY_NAME]
        if bad:
            print("unknown config(s): " + ", ".join(bad), file=sys.stderr)
            print("known: " + ", ".join(BY_NAME), file=sys.stderr)
            return 2
        selected = [BY_NAME[n] for n in args.configs]

    binary = find_headless()
    if not args.dry_run and not binary:
        print("error: izapple2 `headless` binary not found.\n"
              "       build it with `make acceptance-build` (or\n"
              "       go install github.com/ivanizag/izapple2/frontend/headless@latest)\n"
              "       then set IZAPPLE2_HEADLESS=/path/to/headless if it's not on PATH.",
              file=sys.stderr)
        return 1

    workdir = Path(args.workdir) if args.workdir else ROOT / "build" / "acceptance"
    workdir.mkdir(parents=True, exist_ok=True)

    # --disk-dir runs against a pre-built image set (e.g. a release directory):
    # resolve the disks from there and skip the `make` build step entirely.
    if args.disk_dir:
        disk_dir = Path(args.disk_dir)
        if not disk_dir.is_absolute():
            disk_dir = (Path.cwd() / disk_dir)
        if not args.dry_run and not disk_dir.is_dir():
            print(f"error: --disk-dir not a directory: {disk_dir}", file=sys.stderr)
            return 1
        build = False
    else:
        disk_dir, build = DISK_DIR, True
    src = f"pre-built disks in {disk_dir}" if not build else "freshly-built disks"
    print(f"acceptance: {len(selected)} config(s); binary={binary}; "
          f"{src}; results in {workdir}")
    if not build:
        # Reassure: the harness runs off per-config copies (see run_config), so
        # the source images stay pristine even though write scenarios run.
        print(f"acceptance: disks copied into {workdir} per config; "
              f"the originals in {disk_dir} are not modified.")

    # The live window doubles as an interactive controller (start / stop /
    # tick configs), so it drives an interruptible queue loop. A --dry-run has
    # nothing to watch, so it always takes the plain sequential path.
    window = LiveWindow(CONFIGS) if (args.window and not args.dry_run) else None
    if window:
        window.set_selection(selected)       # start with these ticked
        window.open()                        # show the page so the user can Start
        print("acceptance: tick the configs you want, then press Start in the window.")
        try:
            results = run_interactive(window, binary, workdir, args.show,
                                      disk_dir=disk_dir, build=build)
        finally:
            window.close()
        return print_report(results)

    # Sequential (unattended) path: time the whole run — start the clock at the
    # first config, stop it when the last one finishes.
    t0 = time.time()
    results = [run_config(c, binary, workdir, args.dry_run, args.show,
                          disk_dir=disk_dir, build=build)
               for c in selected]
    elapsed = None if args.dry_run else time.time() - t0
    return print_report(results, elapsed)


if __name__ == "__main__":
    sys.exit(main())
