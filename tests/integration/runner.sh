#!/usr/bin/env bash
# tests/integration/runner.sh — runs every *.swift fixture through the
# host build of SwiftII and diffs against its sibling .expected file.
#
# Has zero fixtures. Adds 001_hello.swift et al. once
# the file_runner is wired up. Until then this script just exits 0.

set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
shopt -s nullglob

fixtures=("$here"/[0-9]*.swift)
if [[ ${#fixtures[@]} -eq 0 ]]; then
  echo "integration: no fixtures yet (expected for Phase 0)"
  exit 0
fi

bin="$here/../../build/host/swiftii_host"
if [[ ! -x "$bin" ]]; then
  echo "integration: build $bin first (make test)"
  exit 1
fi

failed=0
total=0
for swift in "${fixtures[@]}"; do
  expected="${swift%.swift}.expected"
  stdin_file="${swift%.swift}.stdin"
  [[ -f "$expected" ]] || continue
  total=$((total + 1))
  # Pipe an optional .stdin fixture (used by readLine() tests). If
  # absent, the binary inherits the runner's empty stdin.
  if [[ -f "$stdin_file" ]]; then
    run() { "$bin" "$swift" < "$stdin_file"; }
  else
    run() { "$bin" "$swift" </dev/null; }
  fi
  if diff -u <(run) "$expected" >/dev/null; then
    printf 'ok   %s\n' "$(basename "$swift")"
  else
    failed=$((failed + 1))
    printf 'FAIL %s\n' "$(basename "$swift")"
    diff -u <(run) "$expected" || true
  fi
done

printf -- '--- %d integration test(s), %d failed\n' "$total" "$failed"
[[ $failed -eq 0 ]]
