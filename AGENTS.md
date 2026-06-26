# AGENTS.md - Working with SwiftII

This file orients AI coding assistants (primarily **Claude Code** and
**Codex**, which did much of the work on SwiftII, plus Cursor and
others) and human collaborators to the SwiftII project. **Read this and
[`CONSTRAINTS.md`](docs/contributing/CONSTRAINTS.md) at the start of every session
before writing or editing code.** They are short on purpose.

---

## What we're building

SwiftII is a Swift-flavored language interpreter for the Apple II. The
binding target is the Apple II Plus with a 16K language card (64K
total) - the hardest fit and the one budgets are set against. The //e
(aux RAM, 80-column) and Saturn 128K cards get more headroom and extra
features, and the original ][ boots too; see
[`CONSTRAINTS.md`](docs/contributing/CONSTRAINTS.md) for the per-machine picture.

It supports three execution paths:

1. **REPL** - user types Swift expressions and statements at a prompt,
   they execute immediately, state persists across the session.
2. **File** - the interpreter reads a `.swift` source file from disk
   and runs it.
3. **Family B compiler/runner** - a separate compiler binary turns a
   `.swift` source file into `.swb` bytecode, which a runner binary
   then executes (the "two execution families" in
   [`ARCHITECTURE.md`](docs/contributing/ARCHITECTURE.md); larger programs,
   //e/Saturn only).

All paths share the same lexer, compiler, and VM. The full language
spec is in [`LANGUAGE.md`](docs/using/LANGUAGE.md). Read that before working on
anything that touches user-visible behavior.

This is a hobby/retrocomputing project. The point is partly the
destination (a working interpreter) and partly the journey (squeezing
it into 64K). Code that works but is twice as large as it needs to be
has failed half the brief.

## Read these before doing anything

1. [`CONSTRAINTS.md`](docs/contributing/CONSTRAINTS.md) - hard limits of the platform and
   toolchain. Treat as binding.
2. [`ARCHITECTURE.md`](docs/contributing/ARCHITECTURE.md) - how the system fits
   together: the pipeline, the value/memory model, and the two binary
   families. Read once for the mental model.
3. [`LANGUAGE.md`](docs/using/LANGUAGE.md) - the SwiftII language spec.
4. [`MEMORY_MAP.md`](docs/contributing/MEMORY_MAP.md) - current zero-page assignments,
   language card layout, heap layout. Update this file when you allocate
   new zero-page slots.
5. [`OPCODES.md`](docs/contributing/OPCODES.md) - current bytecode opcode list with
   semantics and operand layouts. Don't invent new opcodes without
   adding them here.
6. [`LESSONS.md`](docs/contributing/LESSONS.md) - running log of things we've learned the
   hard way (compiler bugs, surprising codegen, allocator quirks). Skim
   before starting any non-trivial change.
7. [`STYLE.md`](docs/contributing/STYLE.md) - coding style (C90 vs C17, formatting, naming,
   comments, functions). Read before writing code.

## Project layout

See [`PROJECT_LAYOUT.md`](docs/contributing/PROJECT_LAYOUT.md) for the full directory tree
and rationale. Short version: production code under `src/` is split by
module (`lexer/`, `compiler/`, `vm/`, `runtime/`, `repl/`,
`file_runner/`, `swb/` for the Family B compiler/runner, `editor/`
(compiled into the boot launcher, which itself lives under
`tools/apple2/boot_launcher/`), `platform/`, `main/`, and shared
`common/`); tests under `tests/` split by layer
(`unit/`, `sim/`, `integration/`, `repl/`) - see
[`TESTING.md`](docs/testing/TESTING.md).

## How we work

### Test-first, host-first

The default workflow is:

1. Write a failing host-side test in `tests/unit/` or
   `tests/integration/`.
2. Implement until it passes on the host (`make test`).
3. Cross-compile with cc65 (`make apple2`).
4. Run the same logic on a 6502 simulator (`make sim`) to catch
   target-specific bugs (size assumptions, signed/unsigned, etc.).
5. Only then build a disk image and try it in an emulator (`make run`).

Steps 1–3 should take seconds. Don't reach for the emulator early; it's
slow and a poor debugging environment compared to the host.

### Test both modes

When adding a feature, add tests for **both** REPL and file mode where
the behavior could differ:

- File mode: `tests/integration/NNN_feature.swift` plus
  `NNN_feature.expected`.
- REPL mode: `tests/repl/NNN_feature.repl` (a script of input lines and
  expected output lines).

Most language-feature tests should pass identically in both. The
exceptions worth watching:

- **Statement-vs-expression behavior** - the REPL implicitly
  `print()`s the value of a bare top-level expression (so `1 + 2`
  yields `3`, BASIC / Python-style); `let`/`var` declarations and
  assignments are silent. File mode discards bare-expression values
  entirely and produces no output.
- **Function redefinition** - on the //e binaries (SWIFTIIE /
  SWIFTAUX) redefining a `func` name rebinds it to the new body and
  prints a `redef foo` notice; the II+ binaries (SWIFTIIP / SWIFTSAT)
  still reject a duplicate name (they sit at the ProDOS file ceiling),
  so `:reset` is the recovery path there. See `LANGUAGE.md`.

### Small changes

A "task" should be at most a few hundred lines of diff. If a task is
bigger than that, split it. Good unit sizes:

- Add one bytecode opcode end-to-end (compiler emits it, VM executes
  it, test verifies it).
- Implement one Swift feature (e.g., `if let` desugaring) on top of
  existing opcodes.
- Add one builtin function with its tests.
- Refactor one module without changing behavior, with the test suite
  green before and after.

### Commit hygiene

Commits should pass `make test`, `make sim`, and `make apple2` (clean
build via cc65, no warnings, no size budget violations). Run all
three before declaring a task done.

### Surface design decisions explicitly

If you're about to make a non-obvious choice - a new zero-page
allocation, a change to the value representation, a new heap structure,
anything that touches the memory map or the bytecode format - stop and
propose it before implementing. Write the proposal as a short markdown
document under `docs/contributing/design/NNN-short-name.md`: *what*, *why*,
*alternatives considered*, *cost*. Get a thumbs-up before coding.

This is the single biggest lever for keeping the project on the rails.
Architectural drift in a 64K project compounds fast.

## How to ask for help

I (the human) am happy to make decisions but bad at noticing implicit
ones. When you need a call from me, ask explicitly with options:

- Good: "The parser needs to handle trailing closures. Two ways: (a)
  special-case in the call expression rule, ~30 LOC; (b) general
  'expression continuation' mechanism that also helps later for `if let`
  chains, ~120 LOC. Which?"
- Bad: open-ended "how should I do X?" without options.

If you genuinely don't know the options, do 15 minutes of research
first and come back with two or three.

## Style

See [`STYLE.md`](docs/contributing/STYLE.md). It is the source of truth - don't rely on
remembered conventions, re-read it when in doubt.

## Tooling

Full toolchain setup and Makefile reference is in
[`BUILDING.md`](docs/contributing/BUILDING.md). Day-to-day:

- `make test`     - host unit tests (clang, fast)
- `make sim`      - bytecode tests via py65
- `make apple2`   - full target build via cc65
- `make disks`    - build the nine-disk set (4 REPL program disks +
                    data disk + 4 Family B compiler disks)
- `make run`      - boot Mariani with the II+ lite disk
- `make size`     - reports section sizes vs. budgets
- `make ci`       - clean + all of the above; run before pushing

`make size` is checked in CI and fails if any section exceeds its
budget in [`CONSTRAINTS.md`](docs/contributing/CONSTRAINTS.md). If your change blows a
budget, the fix is almost never "raise the budget."

## What "done" looks like for a task

- New code has tests (host-side, and REPL/file integration tests where
  user-visible behavior changes).
- Existing tests still pass.
- `make apple2` is clean (no warnings, no size budget failures).
- Any new zero-page or heap layout decisions are reflected in
  `MEMORY_MAP.md`.
- Any new opcodes are documented in `OPCODES.md`.
- Any new language constructs are reflected in `LANGUAGE.md`.
- If you learned something non-obvious, append to `LESSONS.md` (one
  paragraph is fine).
- The commit message says *what* changed and *why*, and references the
  task description.

### Cutting a release

When you cut a new release build (`make release`, a version bump in
[`src/common/version.h`](src/common/version.h), or a new
`releases/v<version>/` folder), **refresh the release data so the docs
match the bits you shipped**:

1. Run `make size` and update the **binary footprints** table (disk-space
   bytes + headroom) in [`FEATURES.md`](docs/using/FEATURES.md).
2. Update the **memory budgets / usage** (BSS, heap, arena, stack-reserve
   numbers) in [`FEATURES.md`](docs/using/FEATURES.md).
3. **On top of `FEATURES.md`, refresh [`MEMORY_MAP.md`](docs/contributing/MEMORY_MAP.md)**
   so the address map matches the bits you shipped: any changed buffer
   size, BSS ceiling, stack reserve, zero-page slot, or heap / LC layout.
   This is a separate, mandatory step every release build — do not assume
   the feature-cost refresh covers it.
4. Re-check the **section budgets** in
   [`CONSTRAINTS.md`](docs/contributing/CONSTRAINTS.md) and the
   [`API.md` limits table](docs/using/API.md#limits-at-a-glance).
5. Update the **disk set** description (per-disk free space, file list) in
   [`releases/README.md`](releases/README.md) and the `progdisk` README if
   the disk contents changed.
6. Bump any **`make size` snapshot date** noted in `FEATURES.md`.

`make release` only *builds* the nine images — it does **not** boot or
verify them, and the acceptance harness is **not** part of the release
process (it is far too slow to run on every build). Booting the disks
through the acceptance suite is a separate step the human runs on their own
time — see "What I (the human) will do" below — so do not run `make
acceptance` yourself as part of cutting a release.

In short: every release, re-derive disk size, memory map, and usage from
the actual build — never carry stale numbers forward.

## What I (the human) will do

- Provide direction, taste, and architectural calls.
- Run the emulator for ambiguous cases that the simulator can't catch.
- Run the **acceptance suite** before tagging a release — on my own time,
  since it boots every disk and is slow. Best run with the live browser
  window so I can watch progress:
  ```sh
  make acceptance ARGS=--window
  ```
  To boot the **exact staged images** instead of a fresh build, add
  `RELEASE=releases/v<version>/` (the harness copies them into
  `build/acceptance/` first, so the staged files are never modified).
  This is a manual gate, deliberately kept out of `make release` and CI.
- Make the final call on memory tradeoffs.
- Keep [`LANGUAGE.md`](docs/using/LANGUAGE.md) and
  [`ROADMAP.md`](docs/contributing/ROADMAP.md) up to date.
- Periodically re-read `AGENTS.md` and `CONSTRAINTS.md` and prune them.

---

## TL;DR for impatient sessions

1. Read `CONSTRAINTS.md`, `LANGUAGE.md`, and the relevant module doc
   (`MEMORY_MAP.md` / `OPCODES.md`) before coding.
2. Tests first, on the host. Cover REPL **and** file mode where they
   could diverge.
3. No `malloc`, no `printf`, no float, no recursion, no surprises.
4. See [`STYLE.md`](docs/contributing/STYLE.md) for the coding-style rules
   (C90 cross / C17 host, naming, comments).
5. Propose architectural changes before making them.
6. Update the docs when you change the things they describe.
