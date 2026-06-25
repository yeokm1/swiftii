#!/usr/bin/env bash
# scripts/setup.sh — provision a fresh dev machine for SwiftII.
#
# Idempotent: safe to re-run. Each step probes for what's already there
# and skips work if it can. Prints a summary at the end with whatever
# the human still needs to do (PATH/JAVA_HOME exports).

set -euo pipefail

repo_root=$(cd "$(dirname "$0")/.." && pwd)
cd "$repo_root"

say() { printf '\033[1;36m[setup]\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m[setup]\033[0m %s\n' "$*" >&2; }

# ---------------------------------------------------------------------------
# Homebrew
# ---------------------------------------------------------------------------

if ! command -v brew >/dev/null 2>&1; then
  warn "Homebrew not found. Install from https://brew.sh and re-run."
  exit 1
fi

# ---------------------------------------------------------------------------
# cc65 (driver, compiler, assembler, linker)
# ---------------------------------------------------------------------------

if ! command -v cl65 >/dev/null 2>&1; then
  say "installing cc65 via Homebrew"
  brew install cc65
else
  say "cc65 already installed: $(cl65 --version 2>&1 | head -1)"
fi

# ---------------------------------------------------------------------------
# OpenJDK (AppleCommander needs Java)
# ---------------------------------------------------------------------------

if [[ ! -x /opt/homebrew/opt/openjdk@21/bin/java ]] \
   && ! /usr/bin/java -version >/dev/null 2>&1; then
  say "installing openjdk@21 for AppleCommander"
  brew install openjdk@21
fi

# ---------------------------------------------------------------------------
# py65 (6502 simulator for `make sim`)
# ---------------------------------------------------------------------------

if ! python3 -c 'import py65' >/dev/null 2>&1; then
  say "installing py65 via pip3 --user"
  pip3 install --user --break-system-packages py65 \
    || pip3 install --user py65
else
  say "py65 already installed"
fi

# ---------------------------------------------------------------------------
# AppleCommander CLI jar
# ---------------------------------------------------------------------------

ac_dir="$repo_root/tools/host"
ac_jar="$ac_dir/AppleCommander-ac.jar"
ac_version="13.0"
ac_url="https://github.com/AppleCommander/AppleCommander/releases/download/${ac_version}/AppleCommander-ac-${ac_version}.jar"

if [[ ! -f "$ac_jar" ]]; then
  say "downloading AppleCommander ${ac_version}"
  mkdir -p "$ac_dir"
  curl -fsSL -o "$ac_jar" "$ac_url"
else
  say "AppleCommander already at $ac_jar"
fi

# ---------------------------------------------------------------------------
# ProDOS 2.4.3 bootable .po image (used as a template by build_po.sh)
# ---------------------------------------------------------------------------

prodos_dir="$repo_root/tools/host/diskimg/prodos243"
prodos_po="$prodos_dir/ProDOS_2_4_3.po"
prodos_url="https://releases.prodos8.com/ProDOS_2_4_3.po"

if [[ ! -f "$prodos_po" ]]; then
  say "downloading ProDOS 2.4.3 .po template"
  mkdir -p "$prodos_dir"
  # The releases.prodos8.com cert sometimes presents the parent CN; -k
  # is acceptable here because we're fetching a public well-known image.
  curl -fsSL -k -o "$prodos_po" "$prodos_url"
else
  say "ProDOS 2.4.3 .po template already at $prodos_po"
fi

# ---------------------------------------------------------------------------
# Summary + shell hints
# ---------------------------------------------------------------------------

cat <<EOF

setup done.

Add to your shell rc (~/.zshrc or ~/.bashrc) so make targets find the tools:

  export PATH="/opt/homebrew/opt/openjdk@21/bin:\$PATH"
  export APPLECOMMANDER_JAR="$ac_jar"

Then in this shell, either re-source the rc or run those lines once. After
that, \`make ci\` and \`make run\` will work.
EOF
