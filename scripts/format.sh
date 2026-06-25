#!/usr/bin/env bash
# scripts/format.sh — runs clang-format on every src/ C file in place.
#
# clang-format reads .clang-format at the repo root.

set -euo pipefail

repo_root=$(cd "$(dirname "$0")/.." && pwd)
cd "$repo_root"

if ! command -v clang-format >/dev/null 2>&1; then
  echo "clang-format not found — \`brew install clang-format\`" >&2
  exit 1
fi

find src tests/unit -name '*.c' -o -name '*.h' | xargs clang-format -i
echo "format: ok"
