# datadisk/

Sources **exclusive** to the SwiftII **data disk** (`make disk-data` →
`swiftii-data.po`, a non-boot ProDOS volume you mount in drive 2). Everything
that also ships on a program/binary disk — the on-disk Help text and the demo
samples that ride along on each program disk — lives under
[`progdisk/`](../progdisk) instead; no source file is duplicated across the two
trees. What remains here is the content that ships **only** on the data disk:

| Folder | On-disk dir | What |
|--------|-------------|------|
| `xsamples/`        | `XSAMPLES/`        | the oversize showcases — `xbig`, `xgrdemo`, `xfuncs` (too big for a program disk) |
| `tests/core/`      | `TESTS/CORE/`      | general self-checking tests (any REPL) |
| `tests/xtests/`    | `TESTS/XTESTS/`    | extras-builtin tests (SWIFTSAT / SWIFTAUX REPL) |
| `tests/fbtests/`   | `TESTS/FBTESTS/`   | Family-B compiler-runner tests |
| `tests/errtests/`  | `TESTS/ERRTESTS/`  | error-message **demos** (deliberately fail; not self-checking) |

The test tiers are nested under one walkable `tests/` tree (on-disk `TESTS/`) so
the Phase 17 auto-test harness (design doc 018) can sweep a single root. The
self-checking tests live only here — see [`tests/README.md`](tests/README.md)
for the `chk` harness, how to run a tier, and the constraints, and each tier's
own README for its file list.

The harness binary itself, **`TESTRUN.SYSTEM`**, also ships on this disk (added
by `build_data_po.sh`, not checked in as a source) — it needs the `TESTS/` tree,
and the program disks have no room for it. With the data disk in drive 2, the
boot launcher's **Run tests (data disk)** menu option chains
`/SWIFTII.DATA/TESTRUN.SYSTEM` to run the sweep.

## The data disk is the canonical FULL set

Although the source split keeps program-disk samples under `progdisk/`, the data
disk **image** is still the complete reference set: its build
(`tools/host/diskimg/build_data_po.sh`, driven from the `Makefile`) assembles
`SAMPLES/` + `XSAMPLES/` from **all** the trees —

- `SAMPLES/` and the small extras-REPL `XSAMPLES/` (`xgraphics`/`xspeaker`/
  `xvtab`/`xsnake`) come from `progdisk/` (referenced, not copied here),
- the **Family-B-only** `XSAMPLES/` (`xdice`/`xwide`, `random`/`switch`/`for-in`)
  come from `progdisk/fbsamples/` — they ship here but **never** on a REPL disk
  (they reject on every Family A REPL), and
- the oversize `XSAMPLES/` showcases below come from `datadisk/xsamples/`.

So a two-drive user gets every demo plus the full tiered test suite from drive 2,
while the source tree stays free of duplicates.

## Oversize showcases — `xsamples/`

These exceed the 2 KB Family-A staging cap, so they ship **nowhere but** the data
disk; run them with two drives (the `*-2disk` run targets mount it). The Family B
Compiler streams source from disk (doc 016) with no such cap.

**Which demo runs on which Family B compiler tier** (the disk you boot picks the
tier; the data disk in drive 2 is the same for all three):

| demo      | Tier 1 flat II+ | Tier 2 Saturn | Tier 3 //e aux | prints           |
|-----------|:---------------:|:-------------:|:--------------:|------------------|
| `xbig`    | ✅              | ✅            | ✅             | `checksum = 6265`|
| `xgrdemo` | ✅              | ✅            | ✅             | `checksum = 1552`|
| `xfuncs`  | ❌ *(too big)*  | ✅            | ✅             | `210`            |

- `xbig.swift` — a ~9 KB, eight-section **number** tour that exercises the
  Compiler's streaming source window. Each section is wrapped in a function so
  its bytecode flushes to the paged store and its arrays/strings free on return,
  which keeps both the paged compile window and the runtime heap small enough to
  run on **all three** tiers (peak ~1.7 KB vs the Saturn Runner's 1,792 B heap).
  The checksum is purely numeric, so the terse labels don't change it. Prints
  `checksum = 6265`.
- `xgrdemo.swift` — the ~7 KB **graphics** companion to `xbig`: five lo-res GR
  scenes (colour bars, concentric squares, a **flying starfield** — the
  old-screensaver kind — a bouncing ball, and a rainbow flash). Column/line
  fills and sparse points only (no per-pixel fills), so it runs briskly. Needs
  the EXTRAS graphics builtins, which the Runner has on every Family B disk; its
  bytecode is small (~1 KB), so it compiles on **any** Family B tier — only its
  source is oversize. Its checksum is pure integer maths (never a screen read),
  so it verifies on the host too: prints `checksum = 1552`.
- `xfuncs.swift` — function-heavy (20 functions); its total bytecode exceeds the
  flat 1,834 B cap, so it compiles + runs **only via paging** (Tier 2 Saturn /
  Tier 3 //e aux) — the deliberate paging showcase, the one demo that the flat
  II+ compiler disk rejects ("compiled bytecode exceeds buffer"). Prints `210`.

(The small extras demos live under `progdisk/` and are documented there: the
extras-REPL ones — `xgraphics`, `xspeaker`, `xvtab`, `xsnake` — in
[`progdisk/xsamples/`](../progdisk/xsamples), and the Family-B-only ones —
`xdice`, `xwide` — in [`progdisk/fbsamples/`](../progdisk/fbsamples).)

## A note on line width (40 columns)

Like the program-disk samples, these are authored for the on-device 40-column
editor (minus a line-number gutter). See
[`progdisk/README.md`](../progdisk/README.md#a-note-on-line-width-40-columns)
for the full line-budget rules; the 80-column showcases (`xwide`, `xvtab`)
wrap their comments to the 80-col editor's budget, exempting only their wide
`print()` output literals.
