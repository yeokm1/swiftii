#!/usr/bin/env bash
# tests/repl/runner.sh — feeds each *.repl transcript into the host
# binary's REPL on stdin and diffs the program output (post-banner,
# post-prompts) against the corresponding .expected file.
#
# A .repl file is the user input: one input line per file line. A
# .expected file captures everything the REPL printed in response,
# minus the banner and prompts. The filter strips:
#   - the three banner lines (SwiftII ..., Copyright ..., Type :help...)
#   - any prompt `> ` prefix on a line (collapsed when a line emits no
#     output of its own and a `> ` runs into the next response)
#   - empty lines (so the diff is line-oriented on program output)
# Will replace this ad-hoc filter with a structured session
# format that handles multi-line continuation prompts.

set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
shopt -s nullglob

# REPL_DIR / REPL_BIN let a caller point the same harness at a different
# fixtures directory and interpreter binary — e.g. the //e-only REPL features
# (function redefinition, line history), which are gated to a no-WITH_SWB
# WITH_IIE build the default host binary doesn't carry. Defaults reproduce the
# original behaviour (this directory, the standard host binary).
dir="${REPL_DIR:-$here}"
fixtures=("$dir"/[0-9]*.repl)
if [[ ${#fixtures[@]} -eq 0 ]]; then
  echo "repl: no transcripts"
  exit 0
fi

bin="${REPL_BIN:-$here/../../build/host/swiftii_host}"
if [[ ! -x "$bin" ]]; then
  echo "repl: build $bin first (make host)"
  exit 1
fi

strip_repl() {
  # Banner is two lines: `SwiftII... X.Y.Z` (machine-tagged name +
  # version; build date + copyright moved to the boot launcher) and
  # `Type :help ...`. The first sed range deletes the banner block. The
  # pattern matches the machine-tagged banner name — host/generic extras
  # is `SwiftIIX`, on-disk builds are `SwiftII ][+`, `SwiftII //e`,
  # `SwiftII ][+ Saturn`, `SwiftII //e aux` — i.e. anything starting with
  # `Swift` followed by a letter, then space. Then strip any run of plain
  # `> ` prompts and discard blank lines.
  sed -E '/^Swift[A-Za-z]+ /,/^Type :/d; s/^(> )+//; /^$/d'
}

failed=0
total=0
for repl in "${fixtures[@]}"; do
  expected="${repl%.repl}.expected"
  [[ -f "$expected" ]] || continue
  total=$((total + 1))
  actual=$("$bin" < "$repl" | strip_repl)
  if diff -u <(printf '%s\n' "$actual") "$expected" >/dev/null; then
    printf 'ok   %s\n' "$(basename "$repl")"
  else
    failed=$((failed + 1))
    printf 'FAIL %s\n' "$(basename "$repl")"
    diff -u <(printf '%s\n' "$actual") "$expected" || true
  fi
done

printf -- '--- %d repl test(s), %d failed\n' "$total" "$failed"
[[ $failed -eq 0 ]]
