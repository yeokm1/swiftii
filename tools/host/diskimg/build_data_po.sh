#!/usr/bin/env bash
# tools/host/diskimg/build_data_po.sh — build a ProDOS data disk (non-boot) holding
# the sample .swift programs under SAMPLES/ and the self-checking unit tests
# under TESTS/.
#
# The editor + the samples overflow the 140 KB II+ boot disk and
# leave no room to SAVE user files, so the samples were pulled off that disk
# (see Makefile $(PO_IMAGE)) and live here instead. The unit tests
# (datadisk/tests/) ship here too — they validate the implemented features on
# target hardware / a fresh emulator. Mount this image as a second drive to
# browse/run/edit them; author new programs on the boot disk with the
# launcher's [F] new-file.
#
# The on-disk tests are tiered by the capability they need (design docs 019,
# 011), nested under one TESTS/ tree so the harness can walk a single root and
# you go to the right subdir for your machine:
#   TESTS/CORE/     general  — run on any REPL (lite or extras): the *-2disk targets
#   TESTS/XTESTS/   extras   — need a SWIFTSAT / SWIFTAUX extras REPL
#   TESTS/FBTESTS/  Family B — need a compiler-runner: boot a compiler disk WITH
#                       this data disk in drive 2 (file builtins, design doc 017)
#   TESTS/ERRTESTS/ demos    — deliberately-failing programs that show each error
#                       message on target ([X] on a compiler disk; not self-checking)
#
# Usage: build_data_po.sh <output.po> <samples dir> \
#                         [general dir] [extras dir] [runner dir]
# Required env: APPLECOMMANDER_JAR pointing at the AppleCommander CLI jar.

set -euo pipefail

if [[ $# -lt 2 || $# -gt 6 ]]; then
  echo "usage: $0 <output.po> <samples dir> [general dir] [extras dir] [runner dir] [err dir]" >&2
  exit 1
fi

output=$1
samples_dir=$2
tests_dir=${3:-}
extras_tests_dir=${4:-}
fb_tests_dir=${5:-}
err_tests_dir=${6:-}

if [[ -z "${APPLECOMMANDER_JAR:-}" ]] || [[ ! -f "$APPLECOMMANDER_JAR" ]]; then
  echo "error: APPLECOMMANDER_JAR not set or file missing" >&2
  exit 1
fi

ac() { java -jar "$APPLECOMMANDER_JAR" "$@"; }

echo "build_data_po: formatting $output as /SWIFTII.DATA/"
ac -pro140 "$output" SWIFTII.DATA

# The launcher stages a chosen program into FILE_SRC_SIZE (2048 B) before
# running it, same as the boot disk — keep every .swift within that ceiling.
# AppleCommander's `-p` auto-creates the leading directory from the path, so
# `SUBDIR/NAME.SWIFT` makes SUBDIR/ on the first put — no separate mkdir.
src_limit=2048

# add_swifts <subdir> <source dir> [limit] — add every *.swift in <source dir>
# under <subdir>/ as an upper-cased NAME.SWIFT (TXT). `ac -p` stores raw bytes
# (TXT is only metadata), so LF line endings are preserved for the lexer. A
# per-call size limit (default $src_limit) skips oversize sources; pass 0 to
# disable it for tiers that run via the streaming Compiler (FBTESTS/), not the
# 2 KB Family-A staging buffer.
add_swifts() {
  local subdir=$1 dir=$2 limit=${3:-$src_limit} f bytes base name
  for f in "$dir"/*.swift; do
    [[ -e "$f" ]] || continue
    bytes=$(wc -c <"$f")
    if (( limit > 0 && bytes > limit )); then
      echo "build_data_po: skipping $f ($bytes B > ${limit} B staging limit)"
      continue
    fi
    base=$(basename "$f")
    name=$(printf '%s' "$base" | tr '[:lower:]' '[:upper:]')
    echo "build_data_po: adding $subdir/$name from $f"
    ac -p "$output" "$subdir/$name" TXT <"$f"
  done
}

# SAMPLES/ = the regular programs (progdisk/samples), added at limit 0. The
# Family B compiler disks mount this in drive 2 and STREAM source; the oversize
# showcases (xbig/xfuncs) arrive via XSAMPLES/ below. A Family A REPL disk with
# this in drive 2 stages content into its 2 KB buffer, so picking an oversize
# sample there truncates (a clean parse error) — those are Family-B/extras
# programs a lite REPL can't run anyway.
add_swifts SAMPLES "$samples_dir" 0
# Extras/Family-B samples (the x-prefixed set) go in their OWN on-disk XSAMPLES/
# directory (mirroring SAMPLES/XSAMPLES like TESTS/XTESTS). The data disk is the
# canonical FULL set, so $SAMPLES_EXTRAS_DIR may name MULTIPLE source dirs
# (space-separated): the program-disk extras (progdisk/xsamples) AND the
# data-disk-only oversize showcases (datadisk/xsamples). All sizes (limit 0).
if [[ -n "${SAMPLES_EXTRAS_DIR:-}" ]]; then
  for xdir in $SAMPLES_EXTRAS_DIR; do add_swifts XSAMPLES "$xdir" 0; done
fi
# The on-disk tests live under one walkable TESTS/ tree (design doc 018), one
# subdirectory per tier. AppleCommander's `-p` auto-creates BOTH path levels
# (TESTS/ and the subdir) on the first put, so no separate mkdir is needed.
# General tests (TESTS/CORE/) and extras tests (TESTS/XTESTS/) stage into the
# Family A 2 KB buffer, so they keep the default limit. Family B tests
# (TESTS/FBTESTS/) run via the Compiler's streaming source window — no cap (0).
[[ -n "$tests_dir" ]]        && add_swifts TESTS/CORE   "$tests_dir"
[[ -n "$extras_tests_dir" ]] && add_swifts TESTS/XTESTS "$extras_tests_dir"
[[ -n "$fb_tests_dir" ]] && add_swifts TESTS/FBTESTS "$fb_tests_dir" 0
# Error-message DEMOS (TESTS/ERRTESTS/) — deliberately-failing programs, run via
# the Family B compiler ([X]); limit 0 (streamed). Not self-checking — see each
# file's header + tests/repl/017_errors for the automated message coverage.
[[ -n "$err_tests_dir" ]] && add_swifts TESTS/ERRTESTS "$err_tests_dir" 0

# Optional: a pre-built oversized `.swb` (env BIG_SWB) for emulator-verifying
# the //e Runner's aux paging — it exceeds what the on-disk Compiler can emit,
# so it's built host-side (tools/host/swbc) and dropped here as BIGPROG.SWB. The
# launcher runs a `.swb` directly on the Runner ([X]), skipping the Compiler.
# Stored as BIN (raw bytes); the launcher keys off the .SWB name, not the type.
if [[ -n "${BIG_SWB:-}" ]]; then
  if [[ ! -f "$BIG_SWB" ]]; then
    echo "error: BIG_SWB=$BIG_SWB not found" >&2; exit 1
  fi
  echo "build_data_po: adding BIGPROG.SWB from $BIG_SWB"
  ac -p "$output" "BIGPROG.SWB" BIN <"$BIG_SWB"
fi

# The on-target auto-test harness (TESTRUN.SYSTEM, design doc 018) lives HERE on
# the data disk, not on the program disks: it can only do anything with this
# disk's TESTS/ tree, and the program disks (especially the Saturn compiler one)
# have no room for it. The boot launcher's [T] command finds this volume among
# the online disks and chains /SWIFTII.DATA/TESTRUN.SYSTEM. SYS file, $2000.
if [[ -n "${TESTRUN_BIN:-}" ]]; then
  if [[ ! -f "$TESTRUN_BIN" ]]; then
    echo "error: TESTRUN_BIN=$TESTRUN_BIN not found" >&2; exit 1
  fi
  echo "build_data_po: adding TESTRUN.SYSTEM from $TESTRUN_BIN"
  ac -p "$output" "TESTRUN.SYSTEM" SYS <"$TESTRUN_BIN"
fi

echo "build_data_po: contents:"
ac -ll "$output"
