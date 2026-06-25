# emulator/

Launcher scripts for whichever Apple II emulator is configured. The
default is **Mariani** on macOS; override with `EMULATOR=<name>` so
`/Applications/<name>.app` is launched instead.

- `run.sh PATH` — boots the chosen emulator with `PATH` (a `.po`
  image). Used by `make run`.
- `run_repl.sh PATH` — compatibility wrapper for REPL boot targets; delegates
  to `run.sh` because SwiftII boots into the launcher/REPL flow itself.
- `run_file.sh PATH` — compatibility wrapper for file-run targets; delegates
  to `run.sh` while disk staging is handled by the Makefile targets.
- `run_izapple2.sh PROFILE` — launches izapple2 in a specific II+/​//e
  hardware profile (Saturn, aux, edge cases). Used by the
  `make run-iz-*` targets. See `make run-configs` / `docs/testing/TESTING.md`.

The `run.sh` wrapper exists so the emulator change is one-stop. To use a
different emulator, edit `EMULATOR` and (if needed) the `open -a`
invocation in `run.sh`.
