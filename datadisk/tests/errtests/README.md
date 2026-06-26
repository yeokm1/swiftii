# datadisk/tests/errtests/

**Error-message demonstrations** — small programs that deliberately fail, so you
can see the on-target error display on an emulator / real hardware. Browse to
`TESTS/ERRTESTS/` on a **Family B compiler disk** and press `[X]` (see
[`../README.md`](../README.md) for mounting the data disk).

These are **not** self-checking "fail 0" programs — they are meant to error, so
they are excluded from the **Run tests** sweep (TESTRUN.SYSTEM). The exhaustive,
automated coverage of every error *message* lives off-target:

- `tests/unit/error_paths_test.c` (`make test` / `make ci`) — one assertion per
  distinct compiler, lexer and VM error string (it pins `cr.err_msg` and the
  runtime `SE_*` code), including the resource-limit messages no short source
  can reach. It also documents the few messages left untested and why.
- `tests/repl/017_errors.{repl,expected}` (`make repl-test`) — the end-to-end
  REPL surface check.

The on-**target** display path these files exercise is itself captured
automatically by the emulator acceptance harness: the `errtests` config
(`make acceptance CONFIGS=errtests`) runs each demo here with `[X]` on a II+
compiler disk, and `errtests-repl` runs the same demos on a REPL disk to cover
the interpreter's own error display — both assert the compile/runtime error
banner + message appear on screen (see
[`tools/host/acceptance/README.md`](../../../tools/host/acceptance/README.md)).

What these on-disk files add, run by hand or by that config, is the
on-**target** display path the host can't:

- **Compile errors** (the Compiler): `compile error: line N: <msg>` with the
  offending source line echoed (//e; the II+ baseline shows the line number with
  a terser message), then "press any key" and chain back to the launcher.
- **Runtime errors** (the Runner): a clean compile, then `runtime error` while
  the program runs, then "press any key" + chain back.

## Why a handful of files, not one per message

A compile error **aborts the whole compilation** — the first error wins and the
rest of the file is never seen. So one file can only ever *demonstrate* one
message, and a file per message would mean ~45 near-identical demos that all
exercise the same display path. Instead each file here demonstrates **one
representative error per family** and lists its siblings in the header; edit the
last line to any sibling's trigger to see that message on target. The full
per-message trigger list is in `error_paths_test.c`.

## The demos

| File | Family | Demonstrates |
|------|--------|--------------|
| `esyntax.swift`  | statement / declaration syntax | `want ')'` (+ the other `want …` / `missing '='` / `unexpected EOF` / `bad type` messages) |
| `enames.swift`   | name resolution & typing | `undeclared name` (+ `type mismatch`, `unknown member`, `need '(...)'`) |
| `enamelen.swift` | identifier length | `name >11 chars` |
| `efuncs.swift`   | functions | `missing return` (+ `void no value`, `bad arg count`, `use positional args, not labels`, `no nested func`) |
| `eflow.swift`    | control flow & switch | `break outside` (+ `return outside`, `for-var let`, `default last`, `nested sw`, `switch Int/Bool`, `want case`, `let is const`) |
| `estrings.swift` | strings & interpolation | `unknown string escape sequence` (+ `empty interpolation`, `bad interp`, the nested/newline/unterminated string-lexer messages) |
| `elex.swift`     | lexer (non-string) | `integer literal out of Int range` (+ `bare '&'`/`'\|'`, `unexpected character`) |
| `eruntime.swift` | runtime | division by zero (+ the other runtime traps, listed in its header) |
| `ebounds.swift`  | runtime | array index out of bounds |

`enamelen.swift` is kept on its own because it demonstrates a specific design
decision: a name longer than the 11-char symbol-table limit (`IDENT_MAX-1`) is a
compile-time error rather than a silent truncation that could collide with
another name on a shared prefix. The same dedicated message fires for
`let`/`var`, function names, parameters, for-in loop variables, and if-let
bindings. See `docs/using/LANGUAGE.md` Identifiers.
