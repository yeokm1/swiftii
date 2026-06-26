#!/usr/bin/env bash
# emulator/run_izapple2.sh — launch a SwiftII disk on izapple2 in one of the
# II+ hardware profiles.
#
# Why izapple2: it models a Saturn 128K, ships with EMBEDDED ROMs (no romset to
# source) and is a single self-contained binary — the lowest-friction path for
# running the II+ binaries.
#
# Usage: run_izapple2.sh <profile> [disk.po]
#
#   profile  model + slot cards                          selects        disk
#   -------  ---------------------------------------      -----------    ----------
#   iip       -model=2plus -s0 language                   SWIFTIIP lite  swiftii-iip-lite-repl.po
#   sat       -model=2plus -s0 saturn                      SWIFTSAT       swiftii-iip-sat-repl.po
#   videx     -model=2plus -s0 saturn -s3 videx            SWIFTSAT       swiftii-iip-sat-repl.po (Videx 80-col)
#   iie       -model=2e                                    SWIFTIIE lite  swiftii-iie-lite-repl.po
#   iienh     -model=2enh                                  SWIFTAUX       swiftii-iie-aux-repl.po (65C02)
#
# Edge / negative cases:
#   ii        -model=2     -s0 language                    SWIFTIIP       II+  Original ][ Integer-BASIC
#                                                                          ROM. Boots + runs programs on
#                                                                          every binary (custom crt0_ibasic
#                                                                          .s: ROM-free _memcpy LC copy, no
#                                                                          $D39A; SWIFTSAT included now).
#                                                                          `C600G` to boot.
#   iip48     -model=2plus  (no language card -> 48K)      (none)         II+  NEGATIVE: ProDOS
#                                                                          needs 64K, so the disk
#                                                                          should fail to boot.
#   sat-s4    -model=2plus -s0 language -s4 saturn          SWIFTSAT       II+  Saturn in a NON-zero
#                                                                          slot — stresses the boot
#                                                                          launcher's slot scan + the
#                                                                          slot-conditional trampoline.
#   memexp    -model=2plus -s0 language -s4 memexp          SWIFTIIP       II+  a non-Saturn RAM card
#                                                                          (slinky) must NOT
#                                                                          false-trigger extras.
#
# The `ii` profile is the original, non-`+` Apple ][ plus a 16K Language Card for
# its 64K. ProDOS itself is NOT the blocker: our ProDOS 2.4.3 (the J.B. Brooks
# "all Apple II" release) boots fine on the original ][ — the PRODOS banner you
# see is it loading. The original blocker was the LAUNCHER (the first `.SYSTEM`
# ProDOS runs): built `-t apple2`, cc65's crt0 UNCONDITIONALLY copies the
# language-card segment at startup via the Applesoft BLTU2 routine ($D39A; ][+ and
# later, absent on the original Integer-BASIC ROM) — even with an empty LC segment
# — so just after ProDOS handed off it JSRed into non-BLTU2 code: izapple2 panics
# / a real ][ + Mariani fall into Integer BASIC, `*** RANGE ERR`.
#
# FIXED (2026-06-04): cc65 2.19 ships no apple2-integer-basic-compat.o and
# hardcodes $D39A, so SwiftII links a custom crt0 (src/platform/apple2/
# crt0_ibasic.s) — upstream crt0 with the BLTU2 call replaced by cc65's own
# __fastcall__ _memcpy (already linked; no-ops an empty LC). It is on the cl65
# line for the launcher AND ALL FOUR interpreters (var A2_CRT0), and is a few
# bytes SMALLER than stock, so every binary fits — including the once-tight
# SWIFTSAT. So the original ][ now boots the menu AND runs programs on any binary
# (plain ][ -> SWIFTIIP; ][ + Saturn -> SWIFTSAT). (It still resets into the
# monitor first: cold-boot `@` garbage then a `*` prompt; `C600G` boots the disk.)
# The copy runs once at startup, so there is no runtime speed cost.
#
# izapple2's //e (`2e`/`2enh`) is always 128K (the aux 64K is part of the
# model), so it does not model a bare-64K or basic-80-col //e. Use Mariani for
# the plain //e 64K/no-aux smoke, and real hardware for basic-80-col. The
# `iienh` profile (65C02) confirms our 6502 code runs on an enhanced //e too.
#
#   language = 16K language card (gives the II+ its 64K so ProDOS boots)
#   saturn   = Saturn-compatible 128K RAM card (defaults to 128K)
#
# Install: download the single-file `izapple2sdl` binary for macOS from
# https://github.com/ivanizag/izapple2/releases (Apple Silicon asset
# izapple2sdl_mac_arm64; Intel: the matching izapple2sdl_mac_* asset),
# `chmod +x` it, and either rename it to `izapple2sdl` on PATH or point at it
# with IZAPPLE2=./izapple2sdl_mac_arm64 — or `go install
# github.com/ivanizag/izapple2/...@latest`. Append flags with
# IZAPPLE2_EXTRA_ARGS=...

set -euo pipefail

IZAPPLE2="${IZAPPLE2:-izapple2sdl}"
profile="${1:-}"
prof_list="ii|iip|sat|videx|iie|iienh|iip48|sat-s4|memexp"
if [[ -z "$profile" ]]; then
  echo "usage: run_izapple2.sh <$prof_list> [disk.po]" >&2
  exit 2
fi

# Default boot disk per profile (the Makefile passes $2 explicitly).
# Each profile boots the single-interpreter image it is meant to exercise.
case "$profile" in
  ii)        slots="-model=2 -s0 language";                  ddisk="build/disk/swiftii-iip-lite-repl.po" ;;
  iip)       slots="-model=2plus -s0 language";              ddisk="build/disk/swiftii-iip-lite-repl.po" ;;
  sat)       slots="-model=2plus -s0 saturn";                ddisk="build/disk/swiftii-iip-sat-repl.po" ;;
  videx)     slots="-model=2plus -s0 saturn -s3 videx";      ddisk="build/disk/swiftii-iip-sat-repl.po" ;;
  iie)       slots="-model=2e";                              ddisk="build/disk/swiftii-iie-lite-repl.po" ;;
  iienh)     slots="-model=2enh";                            ddisk="build/disk/swiftii-iie-aux-repl.po" ;;
  iip48)     slots="-model=2plus";                           ddisk="build/disk/swiftii-iip-lite-repl.po" ;;
  sat-s4)    slots="-model=2plus -s0 language -s4 saturn";   ddisk="build/disk/swiftii-iip-sat-repl.po" ;;
  memexp)    slots="-model=2plus -s0 language -s4 memexp";   ddisk="build/disk/swiftii-iip-lite-repl.po" ;;
  *) echo "error: unknown profile '$profile' (want $prof_list)" >&2; exit 2 ;;
esac

disk="${2:-$ddisk}"

if ! command -v "$IZAPPLE2" >/dev/null 2>&1; then
  echo "error: '$IZAPPLE2' not found on PATH" >&2
  echo "       download izapple2sdl from https://github.com/ivanizag/izapple2/releases" >&2
  echo "       (or set IZAPPLE2=/path/to/izapple2sdl). ROMs are embedded — none to source." >&2
  exit 1
fi
if [[ ! -f "$disk" ]]; then
  echo "error: $disk not found — run \`make disks\` first" >&2
  exit 1
fi

abs_disk=$(cd "$(dirname "$disk")" && pwd)/$(basename "$disk")

# -s6 diskii,disk1=<po> mounts the ProDOS-order image in the slot-6 Disk II and
# boots it. Set DATA_DISK=<path.po> to also mount drive 2 (disk2=) — the Phase
# 14 data disk (`make disk-data`) with the samples / your saved programs.
diskii="diskii,disk1=$abs_disk"
if [[ -n "${DATA_DISK:-}" ]]; then
  if [[ ! -f "$DATA_DISK" ]]; then
    echo "error: DATA_DISK=$DATA_DISK not found — run \`make disk-data\`" >&2
    exit 1
  fi
  abs_data=$(cd "$(dirname "$DATA_DISK")" && pwd)/$(basename "$DATA_DISK")
  diskii="$diskii,disk2=$abs_data"
  echo "run_izapple2.sh: drive 2 = $abs_data"
fi

# $slots is intentionally unquoted so its tokens word-split into separate args
# (none contain spaces); same for IZAPPLE2_EXTRA_ARGS.
echo "run_izapple2.sh: $IZAPPLE2 $slots -s6 $diskii"
# Colour-graphics tip (the xgrdemo / xcolors GR demos): izapple2 boots in its
# NTSC *composite* renderer, which blurs lo-res colour into fringey bands. It has
# no launch flag for the screen mode — press F6 in the window to cycle
# NextScreenMode to "Plain" (flat, pure palette colour, no NTSC artefacts) for a
# crisp recording. F1 toggles the on-screen key help.
echo "run_izapple2.sh: tip — press F6 for the 'Plain' (pure-colour) screen mode; the NTSC composite default blurs GR graphics."
# shellcheck disable=SC2086
exec "$IZAPPLE2" $slots -s6 "$diskii" ${IZAPPLE2_EXTRA_ARGS:-}
