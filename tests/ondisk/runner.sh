#!/usr/bin/env bash
# tests/ondisk/runner.sh — run the SELF-CHECKING on-disk tests
# (datadisk/tests/core + fbtests) through the HOST build and assert each ends
# in "... fail 0" with no VM/compile error.
#
# These tests ship on the data disk and run on TARGET via the emulator
# acceptance harness (make acceptance) — but they are ordinary self-checking
# .swift programs, so running them on the host too gives fast, emulator-free
# regression coverage of the same compiler + VM logic the target exercises
# (e.g. tscope.swift guards the if/else branch local-scoping fix). The host
# can't reach the target-only ground (cc65 codegen, real ProDOS MLI), so this
# complements — does not replace — the acceptance sweeps.
#
# File-I/O tests (tfileio/tfiledir) write relative paths, so each test runs
# from a throwaway CWD to keep side effects out of the tree.
set -uo pipefail

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
bin="$root/build/host/swiftii_host"
if [[ ! -x "$bin" ]]; then
  echo "ondisk: build $bin first (make host)" >&2
  exit 1
fi

shopt -s nullglob
tests=("$root"/datadisk/tests/core/*.swift "$root"/datadisk/tests/fbtests/*.swift)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

failed=0
total=0
for swift in "${tests[@]}"; do
  total=$((total + 1))
  name="$(basename "$(dirname "$swift")")/$(basename "$swift")"
  out=$( cd "$work" && "$bin" "$swift" </dev/null 2>&1 )
  last=$(printf '%s\n' "$out" | grep -iE 'pass [0-9]+ fail [0-9]+' | tail -1)
  if printf '%s\n' "$out" | grep -qiE 'VM halted|runtime error|compile error|exceeds'; then
    printf 'FAIL %-28s (error: %s)\n' "$name" "$(printf '%s\n' "$out" | tail -1)"
    failed=$((failed + 1))
  elif [[ -z "$last" ]] || ! printf '%s\n' "$last" | grep -qiE 'fail 0$'; then
    printf 'FAIL %-28s (verdict: %s)\n' "$name" "${last:-<no tally>}"
    failed=$((failed + 1))
  else
    printf 'ok   %-28s %s\n' "$name" "$last"
  fi
done

echo "--- on-disk host: $total test(s), $failed failed"
[[ $failed -eq 0 ]]
