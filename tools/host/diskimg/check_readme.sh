#!/usr/bin/env bash
# check_readme.sh — verify every built program disk's README.TXT is the correct
# case-fold of the single canonical source, so the on-disk help stays in sync
# across all builds (II+ = UPPER, //e = mixed) and tracks version.h's year.
#
# Run after `make disks disks-familyb`. Needs APPLECOMMANDER_JAR + Java.

set -euo pipefail

if [[ -z "${APPLECOMMANDER_JAR:-}" ]] || [[ ! -f "$APPLECOMMANDER_JAR" ]]; then
  echo "error: APPLECOMMANDER_JAR not set or file missing" >&2
  exit 1
fi
ac() { java -jar "$APPLECOMMANDER_JAR" "$@"; }

year=$(sed -n 's/.*SWIFTII_YEAR[^"]*"\([0-9]*\)".*/\1/p' src/common/version.h)
version=$(sed -n 's/.*SWIFTII_VERSION[[:space:]]*"\([^"]*\)".*/\1/p' src/common/version.h)
D=build/disk
fail=0

# check <image> <canonical source> <upper:0|1> <label> [runner-line]
# runner-line: the per-disk @RUNNER@ expansion (compiler disks only; may embed
# \n for the wrapped line). Empty for REPL disks, whose source has no @RUNNER@.
check() {
  local img=$1 src=$2 upper=$3 label=$4 runner=${5:-}
  if [[ ! -f "$img" ]]; then echo "skip  $label ($img not built)"; return; fi
  local want got tmp built
  tmp=$(mktemp)
  if ! ac -g "$img" README.TXT "$tmp" 2>/dev/null; then
    echo "FAIL  $label: no README.TXT on disk"; fail=1; rm -f "$tmp"; return
  fi
  got=$(cat "$tmp"); rm -f "$tmp"
  # The build timestamp (@BUILT@) is non-deterministic, so read it back from the
  # disk and plug it into the canonical render — everything ELSE (version, year,
  # body) must still match byte-for-byte. Match the line case-insensitively so
  # the II+ UPPER fold ("BUILT ...") is handled too.
  built=$(printf '%s\n' "$got" | sed -n 's/^[Bb][Uu][Ii][Ll][Tt] //p' | head -1)
  if [[ -z "$built" ]]; then
    echo "FAIL  $label: README.TXT has no Built timestamp line"; fail=1; return
  fi
  want=$(sed -e "s/@YEAR@/$year/g" -e "s/@VERSION@/$version/g" -e "s|@BUILT@|$built|g" "$src" \
         | awk -v r="$runner" '{ gsub(/@RUNNER@/, r); print }' \
         | { if [[ "$upper" == 1 ]]; then tr 'a-z' 'A-Z'; else cat; fi; })
  if [[ "$got" == "$want" ]]; then
    echo "ok    $label"
  else
    echo "FAIL  $label: README.TXT differs from the canonical fold"
    diff <(printf '%s\n' "$want") <(printf '%s\n' "$got") | head -8
    fail=1
  fi
}

REPL=progdisk/readme-repl.txt
COMP=progdisk/readme-compiler.txt
# Per-tier @RUNNER@ expansion — MUST match the README_RUNNER values in the
# Makefile's compiler-disk recipes (the \n becomes the wrapped continuation line).
RUN_FLAT='RUNNER   runs the .swb on any\n    II+ (no extra card).'
RUN_SAT='RUNNER   runs the .swb on a II+\n    with a Saturn 128K card.'
RUN_IIE='RUNNER   runs the .swb on a //e\n    with a 64K aux card.'
check "$D/swiftii-iip-lite-repl.po"    "$REPL" 1 "II+ lite REPL"
check "$D/swiftii-iip-sat-repl.po"     "$REPL" 1 "II+ Saturn REPL"
check "$D/swiftii-iie-lite-repl.po"    "$REPL" 0 "//e lite REPL"
check "$D/swiftii-iie-aux-repl.po"     "$REPL" 0 "//e aux REPL"
check "$D/swiftii-iip-compiler.po"     "$COMP" 1 "II+ compiler"          "$RUN_FLAT"
check "$D/swiftii-iip-sat-compiler.po" "$COMP" 1 "II+ Saturn compiler"   "$RUN_SAT"
check "$D/swiftii-iie-compiler.po"     "$COMP" 0 "//e compiler"          "$RUN_IIE"

if [[ $fail -ne 0 ]]; then
  echo "check_readme: FAILED — README.TXT out of sync with the canonical source" >&2
  exit 1
fi
echo "check_readme: all README.TXT in sync with the canonical source"
