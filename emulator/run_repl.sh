#!/usr/bin/env bash
# Compatibility wrapper for REPL boot targets. SwiftII's boot disk handles the
# launcher/REPL flow, so this delegates to the emulator launcher.

exec "$(dirname "$0")/run.sh" "$@"
