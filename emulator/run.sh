#!/usr/bin/env bash
# emulator/run.sh — launch the configured Apple II emulator with a disk
# image. Default emulator is Mariani (macOS-native AppleWin fork).
#
# Usage: run.sh [path/to/disk.po]
#
# Edit EMULATOR to use a different emulator; the spec is in
# docs/contributing/BUILDING.md section 6. (For the CLI-configured Saturn / //e profiles,
# use izapple2 — emulator/run_izapple2.sh.)

set -euo pipefail

EMULATOR="${EMULATOR:-Mariani}"
disk=${1:-build/disk/swiftii-iip-lite-repl.po}

if [[ ! -f "$disk" ]]; then
  echo "error: $disk not found — run \`make disks\` first" >&2
  exit 1
fi

if [[ ! -d "/Applications/${EMULATOR}.app" ]]; then
  echo "error: /Applications/${EMULATOR}.app not found" >&2
  echo "       install Mariani from https://github.com/sh95014/AppleWin" >&2
  echo "       or set EMULATOR=<name> to point at a different one" >&2
  exit 1
fi

# Mariani is AppleWin-based; `-1 PATH` mounts the image in slot 6 drive 1
# and auto-boots, `-2 PATH` mounts slot 6 drive 2. We pass via `open
# --args` so the app's regular launch flow runs (instead of exec'ing the
# binary directly, which would skip macOS app-launch entitlements).
#
# Set DATA_DISK=<path.po> to also mount a second disk in drive 2 (the
# Phase 14 data disk, `make disk-data`): boot disk in drive 1, your
# programs/samples on the data disk in drive 2.
abs_disk=$(cd "$(dirname "$disk")" && pwd)/$(basename "$disk")
args=(-1 "$abs_disk")
if [[ -n "${DATA_DISK:-}" ]]; then
  if [[ ! -f "$DATA_DISK" ]]; then
    echo "error: DATA_DISK=$DATA_DISK not found — run \`make disk-data\`" >&2
    exit 1
  fi
  abs_data=$(cd "$(dirname "$DATA_DISK")" && pwd)/$(basename "$DATA_DISK")
  args+=(-2 "$abs_data")
  echo "run.sh: drive 2 = $abs_data"
fi
echo "run.sh: launching $EMULATOR with $disk"
open -a "$EMULATOR" --args "${args[@]}"
