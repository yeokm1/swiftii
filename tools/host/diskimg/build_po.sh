#!/usr/bin/env bash
# tools/host/diskimg/build_po.sh — produce a bootable ProDOS 2.4.3 .po image
# that auto-launches the SwiftII boot selector at boot.
#
# Usage: build_po.sh <boot launcher> <output.po> <name1> <bin1> [<name2> <bin2> ...]
#
# This builds ONE bootable SYSTEM disk: the boot launcher plus one (or more)
# interpreter binaries. SwiftII ships a 5-DISK set (since the II+ boot disk
# filled up — see docs/contributing/BUILDING.md / ROADMAP): four single-interpreter
# program disks
#
#   swiftii-iip-lite-repl.po   II+ launcher + SWIFTIIP.SYSTEM   (II/II+, //+ typing)
#   swiftii-iip-sat-repl.po    II+ launcher + SWIFTSAT.SYSTEM   (Saturn 128K extras)
#   swiftii-iie-lite-repl.po   //e launcher + SWIFTIIE.SYSTEM   (//e native case)
#   swiftii-iie-aux-repl.po    //e launcher + SWIFTAUX.SYSTEM   (//e 64K-aux extras)
#
# plus a non-boot DATA disk (build_data_po.sh) carrying the samples + the
# on-disk test suite. Each program disk carries ONE interpreter; the boot
# launcher chains whichever binary is present on the booted volume (its
# lite<->extras fallback is bidirectional), so every disk "just works" for the
# machine it targets.
#
# The demo SAMPLES/ also ship on every program disk so a single-floppy-drive
# user has runnable examples without a second drive: set $SAMPLES_DIR to the
# progdisk/samples directory and each *.swift is added under SAMPLES/ (the
# developer-facing TESTS/ stay on the data disk only). The free space now allows
# this — see ROADMAP / the per-disk `make size` numbers.
#
# ProDOS 2.4.3's Bitsy Boot only auto-launches `*.SYSTEM`-suffixed files; the
# boot launcher is installed FIRST as SWIFTII.SYSTEM so ProDOS auto-runs it,
# and it then chains the interpreter. Renames happen at `ac -p` time:
#
#   build/boot_launcher/SWIFTII  → SWIFTII.SYSTEM   (auto-launched launcher)
#   <bin1>                       → <name1>          (e.g. SWIFTIIP.SYSTEM)
#
# Strategy: start from the ProDOS 2.4.3 .po template downloaded by
# scripts/setup.sh, prune every non-PRODOS launcher off it, then drop the
# SwiftII files on in launch order (launcher first).
#
# Required env: APPLECOMMANDER_JAR pointing at the AppleCommander CLI jar.

set -euo pipefail

if [[ $# -lt 4 ]] || (( ($# - 2) % 2 != 0 )); then
  echo "usage: $0 <boot launcher> <output.po> <name1> <bin1> [<name2> <bin2> ...]" >&2
  echo "       one <name> <bin> pair per interpreter/tool to place on the disk" >&2
  echo "       (REPL disks ship one interpreter; compiler disks add Compiler + Runner; see" >&2
  echo "        docs/contributing/BUILDING.md). Samples + tests ship on the data disk" >&2
  echo "        (build_data_po.sh)." >&2
  exit 1
fi

launcher_bin=$1
output=$2
shift 2   # remaining args are <name bin> pairs

if [[ -z "${APPLECOMMANDER_JAR:-}" ]] || [[ ! -f "$APPLECOMMANDER_JAR" ]]; then
  echo "error: APPLECOMMANDER_JAR not set or file missing" >&2
  echo "       run scripts/setup.sh, then export APPLECOMMANDER_JAR" >&2
  exit 1
fi

here=$(cd "$(dirname "$0")" && pwd)
template="$here/prodos243/ProDOS_2_4_3.po"

if [[ ! -f "$template" ]]; then
  echo "error: $template not found" >&2
  echo "       run scripts/setup.sh to fetch it" >&2
  exit 1
fi

ac() { java -jar "$APPLECOMMANDER_JAR" "$@"; }

# These are every non-PRODOS file that ships in the ProDOS 2.4.3
# distribution image. ProDOS would otherwise auto-launch BITSY.BOOT
# (which is the menu/launcher) before reaching our boot launcher. Each
# `-d` is best-effort — already-deleted files just no-op. The list is
# explicit (rather than scraped from `ac -ll`) so the script keeps
# working if the upstream template gains or drops files.
prune_list=(
  VIEW.README
  BITSY.BOOT
  QUIT.SYSTEM
  BASIC.SYSTEM
  COPYIIPLUS.8.4
  BLOCKWARDEN
  CAT.DOCTOR
  UNSHRINK
  CD.EXT
  FASTDSK
  FASTDSK.CONF
  FASTDSK.SYSTEM
  MAKE.SMALL.P8
  MINIBAS
  MR.FIXIT.Y2K
  README
)

echo "build_po: copying template to $output"
cp "$template" "$output"

echo "build_po: pruning template down to PRODOS only"
for f in "${prune_list[@]}"; do
  ac -d "$output" "$f" >/dev/null 2>&1 || true
done

# Order matters: ProDOS picks the first `*.SYSTEM` file in directory order, so
# the boot launcher (installed as SWIFTII.SYSTEM) MUST be added before the
# interpreter binaries.
echo "build_po: adding SWIFTII.SYSTEM (boot launcher)"
ac -p "$output" SWIFTII.SYSTEM SYS <"$launcher_bin"

while (( $# >= 2 )); do
  name=$1
  bin=$2
  shift 2
  echo "build_po: adding $name (interpreter)"
  ac -p "$output" "$name" SYS <"$bin"
done

# The standalone detection diagnostic. Added AFTER SWIFTII.SYSTEM so
# ProDOS still auto-launches the boot launcher; the launcher's Debug menu chains
# DEBUG.SYSTEM, which displays the probe bytes and chains back. $DEBUG_BIN is
# the built binary; ships on every program disk (it's tiny, ~1 KB).
if [[ -n "${DEBUG_BIN:-}" ]]; then
  if [[ -f "$DEBUG_BIN" ]]; then
    echo "build_po: adding DEBUG.SYSTEM from $DEBUG_BIN"
    ac -p "$output" DEBUG.SYSTEM SYS <"$DEBUG_BIN"
  else
    echo "error: DEBUG_BIN=$DEBUG_BIN not found" >&2
    exit 1
  fi
fi

# The on-target auto-test harness (design doc 018). Like DEBUG.SYSTEM it is a
# standalone SYS tool added AFTER SWIFTII.SYSTEM (so ProDOS still auto-launches
# the launcher); the launcher's [T] command and its boot-resume chain it. Ships
# on every program disk; it sweeps the tiered tests on the data disk in drive 2.
if [[ -n "${TESTRUN_BIN:-}" ]]; then
  if [[ -f "$TESTRUN_BIN" ]]; then
    echo "build_po: adding TESTRUN.SYSTEM from $TESTRUN_BIN"
    ac -p "$output" TESTRUN.SYSTEM SYS <"$TESTRUN_BIN"
  else
    echo "error: TESTRUN_BIN=$TESTRUN_BIN not found" >&2
    exit 1
  fi
fi

# The on-disk help text. The boot launcher's old Help menu screen moved out to a
# README.TXT in the disk root (opened from the File selector / editor) to reclaim
# launcher code space; the About screen points the user at it. $README_FILE picks
# the right copy per disk (REPL vs Family B compiler); stored as raw TXT so its LF
# line endings survive for the editor.
if [[ -n "${README_FILE:-}" ]]; then
  if [[ -f "$README_FILE" ]]; then
    echo "build_po: adding README.TXT from $README_FILE (upper=${README_UPPER:-0})"
    # One canonical source per family (readme-repl.txt / readme-compiler.txt).
    # Substitute the release year (@YEAR@) and version (@VERSION@), both kept in
    # sync with version.h, plus the build timestamp (@BUILT@, stamped by the
    # Makefile). @RUNNER@ (compiler disks only) carries the per-disk Runner line:
    # each compiler tier needs a different machine/card (flat = none, Saturn =
    # Saturn 128K, //e = 64K aux), so the value is passed per disk in README_RUNNER
    # and may embed \n (awk -v expands the escape) for its wrapped continuation
    # line. On II+ disks (README_UPPER=1) fold to UPPER CASE — exactly as the
    # launcher's cout_str folds the About screen on the II+ — so the README and the
    # About page stay in sync across every build from one source.
    sed -e "s/@YEAR@/${README_YEAR:-}/g" \
        -e "s/@VERSION@/${README_VERSION:-}/g" \
        -e "s|@BUILT@|${README_BUILT:-}|g" "$README_FILE" \
      | awk -v r="${README_RUNNER:-}" '{ gsub(/@RUNNER@/, r); print }' \
      | { if [[ "${README_UPPER:-0}" == "1" ]]; then tr 'a-z' 'A-Z'; else cat; fi; } \
      | ac -p "$output" README.TXT TXT
  else
    echo "error: README_FILE=$README_FILE not found" >&2
    exit 1
  fi
fi

# Merge: the editor is no longer a separate SWIFTED.SYSTEM file — it
# links into the boot launcher (SWIFTII.SYSTEM) and runs in-process. The
# on-disk test suite ships on the separate data disk (build_data_po.sh); the
# demo samples ship here too when $SAMPLES_DIR is set (below).

# Demo programs. `ac -p DIR/NAME.SWIFT` auto-creates the directory on the first
# put. Names are upper-cased; raw bytes are stored (TXT is metadata only) so LF
# line endings survive for the lexer. The Family A launcher stages a chosen
# program into FILE_SRC_SIZE (2048 B) before running, so on REPL disks each
# .swift must stay within that ceiling — oversize ones are SKIPPED with a notice
# (they could not run there anyway). Family B compiler disks stream source from
# disk (doc 016) with no such cap: those targets pass SAMPLE_SRC_LIMIT=0.
# $SAMPLES_ONLY (optional): a space-separated basename list; when set, only those
# files ship. The Family B disks use it for a MINIMAL inline set (greet +
# functions + xsnake) so the Compiler has free blocks to write .swb output (a
# full disk = MLI $48). The canonical FULL set lives on the data disk.
#
# Two source folders map to two ON-DISK directories (mirroring TESTS/XTESTS):
#   $SAMPLES_DIR        (progdisk/samples)  -> SAMPLES/   regular, any system
#   $SAMPLES_EXTRAS_DIR (progdisk/xsamples) -> XSAMPLES/  x-prefixed extras
# Lite REPL disks pass only $SAMPLES_DIR (no x-programs); extras REPL + Family B
# disks add $SAMPLES_EXTRAS_DIR. Each may name multiple space-separated dirs.
# NOTE the caller controls which extras dirs go where: the Family-B-ONLY demos
# (progdisk/fbsamples, random/switch/for-in) reject on any REPL, so the Makefile
# passes them ONLY on the data disk's $SAMPLES_EXTRAS_DIR list, never a REPL disk.
src_limit=${SAMPLE_SRC_LIMIT:-2048}
add_samples() {  # <on-disk-subdir> <src-dir>...
  local subdir=$1; shift
  local dir f bytes base name
  for dir in "$@"; do
    [[ -n "$dir" ]] || continue
    for f in "$dir"/*.swift; do
      [[ -e "$f" ]] || continue
      if [[ -n "${SAMPLES_ONLY:-}" ]]; then
        case " $SAMPLES_ONLY " in
          *" $(basename "$f") "*) ;;
          *) echo "build_po: skipping $f (not in SAMPLES_ONLY; keeps .swb room)"
             continue ;;
        esac
      fi
      bytes=$(wc -c <"$f")
      if (( src_limit > 0 && bytes > src_limit )); then
        echo "build_po: skipping $f ($bytes B > ${src_limit} B staging limit; Family B disks only)"
        continue
      fi
      base=$(basename "$f")
      name=$(printf '%s' "$base" | tr '[:lower:]' '[:upper:]')
      echo "build_po: adding $subdir/$name from $f"
      ac -p "$output" "$subdir/$name" TXT <"$f"
    done
  done
}
[[ -n "${SAMPLES_DIR:-}" ]]        && add_samples SAMPLES  $SAMPLES_DIR
[[ -n "${SAMPLES_EXTRAS_DIR:-}" ]] && add_samples XSAMPLES $SAMPLES_EXTRAS_DIR

echo "build_po: contents:"
ac -ll "$output"
