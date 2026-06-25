#!/usr/bin/env bash
# Compatibility wrapper for file-run targets. Makefile disk targets handle
# staging source files; this delegates to the emulator launcher.

exec "$(dirname "$0")/run.sh" "$@"
