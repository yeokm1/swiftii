# DEVELOPING.md - developer getting-started guide

The contributor counterpart to the [user tutorial](../using/TUTORIAL.md). If you
want to *build SwiftII, fix a bug, or add a feature*, start here. It tells
you **what to do** and **which documents to read, in order** - it doesn't
duplicate them.

> The canonical working guide is **[AGENTS.md](../../AGENTS.md)** (it applies
> to human and AI contributors alike). This doc orients you *to* it and
> the rest of `docs/`; read AGENTS.md first, then use the reading order in
> section 4 as you go.

---

## 1. What you're working on

SwiftII is a Swift-like language - lexer, bytecode compiler, and VM -
written in **portable C** that builds two ways:

- a **host** build (`clang`) for fast tests and a desktop interpreter, and
- **Apple II** builds (`cc65`) that fit inside ProDOS's **40,704-byte**
  single-file ceiling on a 64 KB machine.

Almost every constraint in the project flows from that ceiling. The whole
art is "add a feature without busting the budget."

---

## 2. Set up the toolchain

Follow **[BUILDING.md](BUILDING.md)** - it has a one-shot setup and the
manual steps. In short you need:

- standard Unix tools + **`clang`** / **`clang-format`** (host build + style),
- **`cc65`** (the 6502 C compiler - the Apple II builds),
- **`py65`** (a 6502 simulator - `make sim`),
- **AppleCommander** + **ProDOS 2.4.3** files (building `.po` disk images),
- an **emulator** (Mariani and/or the bundled izapple2) to run them.

Verify your setup builds the host interpreter:

```sh
make host
./build/host/swiftii_host progdisk/samples/fizzbuzz.swift
```

---

## 3. The inner dev loop

**Test-first, host-first** (see AGENTS.md's "How we work" section). The five test
layers and when to use each are in **[TESTING.md](../testing/TESTING.md)**; the fast
path:

| Command | What it runs | Speed |
|---------|--------------|-------|
| `make test` | host C unit tests (lexer / compiler / heap / value / vm) | sub-second |
| `make sim`  | bytecode on the py65 6502 simulator | seconds |
| `make integration` | `.swift` programs end-to-end in file mode | seconds |
| `make repl-test` / `repl-test-iie` | scripted REPL sessions | seconds |
| `make host` | the desktop interpreter, for ad-hoc runs | fast |
| `make size` | the Apple II binary footprints vs the ceiling | - |
| `make ci` | **everything** (clean + all tests + all Apple II builds + disks) | the gate |

A typical change: write a failing test → make it pass on the host
(`make test`) → confirm the target codegen agrees (`make sim`) → check you
didn't blow the budget (`make size`) → `make ci` before you commit.

**Build the real binaries / disks** when you need them:

```sh
make apple2-all         # all four REPL interpreters + launchers
make apple2-familyb     # the Family B Compiler + Runner
make disks              # the nine distribution .po images
make run                # boot the II+ lite disk in an emulator
```

Test **both execution modes** for any language change - REPL *and* file
mode behave differently (AGENTS.md's "Test both modes" section).

---

## 4. Reading order

Read AGENTS.md first. Then reach for these as your task needs them - this
is the order that builds context fastest:

1. **[BUILDING.md](BUILDING.md)** - set up + build (you're here once section 2 works).
2. **[PROJECT_LAYOUT.md](PROJECT_LAYOUT.md)** - where everything lives
   (`src/`, `tests/`, `progdisk/`, `datadisk/`, `emulator/`).
3. **The language you're implementing:**
   - **[LANGUAGE.md](../using/LANGUAGE.md)** - the user-visible language spec.
   - **[OPCODES.md](OPCODES.md)** - the bytecode the VM runs.
4. **The hard walls - read before adding anything that costs bytes:**
   - **[CONSTRAINTS.md](CONSTRAINTS.md)** - the budget rules and what
     they forbid.
   - **[MEMORY_MAP.md](MEMORY_MAP.md)** - the Apple II address space, the
     ProDOS ceiling, the Saturn/aux paging.
5. **[STYLE.md](STYLE.md)** - conventions; match the surrounding code.
6. **[TESTING.md](../testing/TESTING.md)** - the five test layers in depth (you met
   them in section 3).
7. **[LESSONS.md](LESSONS.md)** - the hard-won gotchas. cc65 quirks,
   budget traps, "never do X" - skim it early, it will save you a day.
8. **[ROADMAP.md](ROADMAP.md)** + **[ROADMAP-MAYBE.md](ROADMAP-MAYBE.md)** - what's shipped, what's deferred and *why* (cost), referenced by
   number across the docs.
9. **[FEATURES.md](../using/FEATURES.md)** - the feature/cost table; useful when
   deciding whether your feature fits which binary.
10. **[design/](design)** - the numbered design records, for the deep
    "why" behind a subsystem. Read the one for the area you're touching.

---

## 5. Your first change - a worked path

Say you want to add or fix a small language feature:

1. **Find the surface** in [LANGUAGE.md](../using/LANGUAGE.md) and the
   [grammar](../using/LANGUAGE.md#grammar-ebnf-abbreviated); decide the
   user-visible behaviour.
2. **Write a failing test first** - a host unit test in `tests/unit/`
   (lexer/compiler/vm) and/or an integration program in
   `tests/integration/`. See [TESTING.md](../testing/TESTING.md) for the layout.
3. **Implement on the host** - edit the C in `src/` (lexer → compiler →
   vm, as the feature needs), `make test` until green.
4. **Confirm on the target** - `make sim` (codegen parity), then
   `make size` (you're still under the 40,704-byte ceiling on every
   binary; the headroom is *tight* - see [FEATURES.md](../using/FEATURES.md) section 1).
5. **Check both modes** - `make repl-test` *and* `make integration`.
6. **Run it for real** - `make run` (or `run-sat` / `run-aux` /
   `run-iz-*` for the path your feature needs).
7. **Green the gate** - `make ci`.
8. **Commit** small and focused (AGENTS.md's "Commit hygiene" section); update the
   relevant doc(s) and the ROADMAP in the same change.

If your feature *doesn't fit* the budget, that's a normal outcome here -
record the measured cost and move it to [ROADMAP-MAYBE.md](ROADMAP-MAYBE.md)
with the number and rationale. Keep [FEATURES.md](../using/FEATURES.md) focused on
features that ship.

---

## 6. What "done" looks like

The checklist in **AGENTS.md's "What 'done' looks like for a task" section** is
authoritative. The short version: tests written and passing, both modes
exercised, `make ci` green, binaries under budget (`make size`), docs and
ROADMAP updated, and a clean focused commit.
