# ROADMAP.md

This roadmap is a project-status index. It should be long enough to
orient a contributor, but short enough that detailed implementation
records stay in the right place.

Use this file for:

- the phase sequence and dependencies,
- what each phase was trying to unlock,
- what shipped at a product level.

Use the other docs for detail:

- Detailed designs live in [`docs/contributing/design/`](design/).
- User-visible language and builtins live in
  [`LANGUAGE.md`](../using/LANGUAGE.md).
- Memory layout lives in [`MEMORY_MAP.md`](MEMORY_MAP.md).
- Bytecode details live in [`OPCODES.md`](OPCODES.md).
- Backlog ideas live in [`ROADMAP-MAYBE.md`](ROADMAP-MAYBE.md).

ProDOS 2.4.3 is the only target operating system.

Current status: **Phases 0-17 are closed. v1.0.1 is the released version.**

---

## Phase 0 - Scaffolding

Goal: a "hello world" interpreter that can run one hardcoded bytecode
program on both host and Apple II.

Status: **closed**.

Rationale: this established the inner loop before the language
existed. The project needed proof that clang, cc65, ProDOS image
building, emulator boot, and a shared host/target source tree could all
work together.

What shipped:

- Project skeleton matching the documented layout.
- Makefile targets for host tests, simulator tests, Apple II builds,
  disk images, emulator launch, size reporting, and cleanup.
- Host and Apple II platform stubs with a minimal print interface.
- ProDOS 2.4.3 boot-file staging and an ld65 config for a ProDOS SYS
  binary.
- Host and simulator smoke tests.

The key outcome was confidence that the development loop was fast enough
to support test-first work.

## Phase 1 - Lexer + Minimal Compiler + VM Core

Goal: integer arithmetic and `print` work end-to-end through the real
lexer/compiler/VM pipeline.

Status: **closed**.

Rationale: this replaced the hand-assembled demo with the real
front end and VM path. From this point onward, features could be added by
extending the actual language implementation instead of a throwaway test
program.

What shipped:

- Lexer support for Tier 1 tokens.
- Pratt expression parsing for arithmetic and comparison.
- Statement parsing for declarations, assignments, `print`, and
  expression statements.
- Bytecode emission for constants, arithmetic, variables, and print.
- Tagged `Value` representation for `Int` and `Bool`.
- VM dispatch in C, with integer multiply/divide/modulo helpers.
- Host integration tests and a disassembler tool.

The demo-level milestone was `let x = 21; let y = 2; print(x * y)`
printing `42` through the full pipeline.

## Phase 2 - REPL Mode

Goal: an interactive REPL on top of the shared compiler and VM.

Status: **closed**.

Rationale: SwiftII is not only a file runner. The REPL is the
primary interactive experience on a small Apple II system, and it forced
the compiler and VM to support incremental compilation, persistent
globals, and recoverable errors.

What shipped:

- REPL driver: read, compile, run, repeat.
- Persistent globals and function table state across inputs.
- Host stdin input and Apple II keyboard input.
- Recoverable compile/runtime errors.
- Startup banner and prompt.
- Meta-commands: `:help`, `:quit`, `:list`, `:reset`, and `:mem`.
- Shared lexer/compiler/VM between REPL and file mode.

The important architectural point is that the mode split lives in the
driver, not in separate language implementations.

Design reference:
[`005-repl-design.md`](design/005-repl-design.md).

## Phase 3 - Strings and Control Flow

Goal: programs with strings, branches, loops, and Apple II-appropriate
input/display behavior.

Status: **closed**.

Rationale: this phase moved SwiftII from calculator expressions to
small real programs. It also settled the pre-IIe keyboard/display model,
which affects every user-facing interaction on the baseline machine.

What shipped:

- String literals with escapes.
- Heap-backed strings, concatenation, and interpolation.
- Reference management for heap values.
- `if`, `else`, `else if`, `while`, and `for-in` over ranges.
- Jump and loop bytecodes needed by structured control flow.
- Apple II Plus input translation for lowercase and missing punctuation.
- Pre-IIe display substitution for lowercase and unsupported glyphs.

The demo-level milestone was FizzBuzz running identically in REPL and
file-oriented tests.

Design references:
[`002-heap-and-strings.md`](design/002-heap-and-strings.md),
[`003-apple2-input-method.md`](design/003-apple2-input-method.md).

## Phase 4 - Functions, Optionals, Arrays

Goal: the core "feels like Swift" language surface.

Status: **closed**.

Rationale: functions, optionals, and arrays are the point where the
language starts to resemble Swift rather than a BASIC-like expression
interpreter. This phase also raised the demo limits to values that could
carry meaningful examples.

What shipped:

- Function declarations, calls, returns, argument handling, and locals.
- VM call frames and call-stack save/restore.
- Compile-time function signature tracking.
- Optionals: `Int?`, `String?`, `nil`, `!`, `??`, and single-binding
  `if let`.
- Arrays: literals, empty arrays, subscript reads, `.count`, `.append`,
  inline printing, subscript writes, and `.isEmpty`.
- `readLine() -> String?`.
- `break` in loops.
- `print(_:terminator:)`.
- Larger source, bytecode, function, global, local, and identifier limits.

The demo-level milestone was a small input-processing program that reads
values, stores them, and prints a running result.

Design references:
[`004-demo-oriented-scope.md`](design/004-demo-oriented-scope.md),
[`007-arrays.md`](design/007-arrays.md).

## Phase 5 - REPL Polish

Goal: heap-aware REPL introspection and recovery.

Status: **closed**.

Rationale: once the language had heap objects and persistent REPL
state, users needed a way to inspect memory pressure and recover cleanly
without rebooting.

What shipped:

- `:mem` reports heap availability.
- `:reset` clears bindings and resets the heap.
- Startup/banner and meta-command behavior were tightened around the
  small-binary budget.

User-facing behavior is documented in
[`LANGUAGE.md`](../using/LANGUAGE.md).

## Phase 6 - Optimization and Size Reduction

Goal: recover headroom for the next language and platform work.

Status: **closed**.

Rationale: the Apple II ProDOS SYS ceiling is a hard constraint.
This phase converted size pressure into a regular engineering input:
profile, measure, reclaim, and document the trade.

What shipped:

- Benchmark hooks and repeatable size measurements.
- Compiler flag tuning for the hot paths.
- Cross-translation-unit error-string deduplication.
- Zero-page VM state for selected hot values.
- Smaller retain/release fast paths.

This phase did not make the project roomy. It made later budget choices
explicit and measurable.

Design reference:
[`008-phase6-optimization.md`](design/008-phase6-optimization.md).

## Phase 7 - Tier 2 Language Features

Goal: make programs more Swift-like while staying inside the 64K
interpreter budget.

Status: **closed**.

Rationale: by this point the parser accepted enough syntax that
better type feedback became more valuable than adding many new constructs.
The phase tightened correctness without changing the language's basic
shape.

What shipped:

- Type tracking for declarations, optionals, arrays, and function
  signatures.
- Better compile-time mismatch diagnostics.
- Array element-type tracking.
- `min` and `max`.
- `String(_:)` for integer-to-string conversion.
- Array subscript assignment.
- Array `.isEmpty`.

Design reference:
[`009-type-tracker.md`](design/009-type-tracker.md).

## Phase 8 - Hardware Capability Detection and Build Matrix

Goal: split the interpreter by hardware capability so optional features
have a budget home.

Status: **closed**.

Rationale: a single universal binary could not carry every feature.
The project needed a principled way to keep the 64K baseline useful while
letting larger machines pay for extra capability.

What shipped:

- Boot launcher with hardware detection and binary chaining.
- Lite and extras interpreter families.
- Size reporting and budget checks for multiple Apple II binaries.
- Disk-image build support for the split binaries.
- A launcher foundation later reused for file browsing, editing,
  diagnostics, test running, and compiler/runner workflows.

Design references:
[`010-hardware-tiers.md`](design/010-hardware-tiers.md),
[`011-extras-lc-in-saturn-aux.md`](design/011-extras-lc-in-saturn-aux.md).

## Phase 9 - Tier 2 Language Carryovers

Goal: finish high-value language/runtime features that needed the
multi-binary budget split.

Status: **closed**.

Rationale: Phase 8 created a home for features that were too costly
for the smallest interpreter. Phase 9 proved that model by moving cold
runtime work into the extras path.

What shipped:

- `Int(_:) -> Int?` for parsing decimal strings.
- `asc(_:)` and `chr(_:)`.
- Array methods such as remove/contains operations on supported builds.
- Function-body `if let` and `if let ... else` behavior.
- Saturn XLC dispatch for cold extras builtins.

Design reference:
[`011-extras-lc-in-saturn-aux.md`](design/011-extras-lc-in-saturn-aux.md).

## Phase 10 - Apple II Platform APIs

Goal: expose Apple II-native text, graphics, sound, and memory primitives
through Swift-style builtins.

Status: **closed**.

Rationale: the project is specifically for the Apple II. This phase
made programs feel native to the machine instead of being portable code
that happens to run there.

What shipped:

- Text and cursor controls such as `home`, `text`, `htab`, and `vtab`.
- Memory access through `peek` and `poke`.
- Low-resolution graphics: `gr`, `grFull`, `color`, `plot`, `hlin`,
  `vlin`, and `scrn`.
- Speaker click support through the existing memory-access path.
- Host stubs where needed so tests remain deterministic.

The public API is documented in
[`LANGUAGE.md`](../using/LANGUAGE.md).

Design reference:
[`012-phase10-platform-builtins.md`](design/012-phase10-platform-builtins.md).

## Phase 11 - IIe Aux Extras Binary

Goal: give Apple //e systems with aux RAM the extras surface.

Status: **closed**.

Rationale: Saturn and aux RAM solve different problems. Saturn can
execute banked code in the language-card region; //e aux RAM needs a
copy-down model. Supporting both made the extras story practical for more
real machines.

What shipped:

- `SWIFTAUX.SYSTEM`.
- Aux-RAM copy-down staging for cold XLC bodies.
- Boot-launcher routing for aux-capable systems.
- Shared extras builtin surface across Saturn and aux where the hardware
  model allows it.
- //e-native case input and lowercase display for //e builds.
- Aux park and staging layout documented in `MEMORY_MAP.md`.

Design reference:
[`011-extras-lc-in-saturn-aux.md`](design/011-extras-lc-in-saturn-aux.md).

## Phase 12 - 80-Column Text Mode

Goal: support 80-column text where the hardware and binary budget allow it.

Status: **closed**.

Rationale: 80-column output changes how usable the REPL, editor,
and file programs feel on capable machines. It also exposed a real split
between //e firmware 80-column behavior and II+ third-party card behavior.

What shipped:

- //e 80-column output through firmware COUT.
- `text80` and `text` builtins on supported //e binaries.
- 80-column-aware cursor positioning.
- Automatic //e lite behavior where appropriate for the disk.
- II+ Videx support later completed in Phase 16.

Design reference:
[`013-80col-text.md`](design/013-80col-text.md).

## Phase 13 - Run Swift Programs From Disk

Goal: let users browse disks and run saved `.swift` files on target
hardware.

Status: **closed**.

Rationale: without an Apple II command line, the interpreter needed
a period-appropriate way to choose a program. This phase made saved source
files first-class on target hardware.

What shipped:

- Boot-launcher menu and file browser.
- Program staging in low RAM before chaining to an interpreter.
- File execution from the browser.
- Return-to-launcher behavior from `:quit`.
- Resume behavior, file preview, and ProDOS file operations.
- Shared staging primitives later reused by the editor and compiler tools.

Design reference:
[`014-run-from-disk.md`](design/014-run-from-disk.md).

## Phase 14 - Editor + Data Disk

Goal: edit Swift programs on target hardware and provide a data disk for
examples and tests.

Status: **closed**.

Rationale: running saved programs is only half the experience. A
usable on-target workflow needs editing, saving, running, and returning to
the editor without leaving the Apple II environment.

What shipped:

- Editor merged into the boot launcher.
- Program disks with launcher, editor, interpreter, diagnostics, and
  samples.
- Data disk with the full sample and self-checking test set.
- Save, save-as, run, word wrap, file-too-large handling, and editor
  responsiveness work.
- II+ cooked/raw editing mode for Swift source versus general text.
- //e 80-column editor rendering and width toggle.

Design reference:
[`006-editor.md`](design/006-editor.md).

## Phase 15 - On-Disk Compiler + Runner

Goal: lift the staged-source cap by splitting compiled programs into a
Compiler and Runner toolchain.

Status: **closed**.

Rationale: the Family A interpreters compile and run in one
resident image, so they are bounded by small in-RAM source, bytecode, and
heap buffers. A separate Compiler and Runner lets larger file programs
trade interactivity for capacity.

What shipped:

- Family B compiler disks.
- `Compiler` binary that reads source and writes `.swb`.
- `Runner` binary that loads `.swb` and executes it.
- `.swb` serialization for bytecode, constants, and runtime function
  metadata.
- Disk-backed source window for larger source files.
- Family B file I/O builtins.
- Return-to-launcher flow after compile/run.
- Tiered toolchain support for II+, II+ Saturn, and //e aux.

Design references:
[`015-bigger-programs-pascal-toolchain.md`](design/015-bigger-programs-pascal-toolchain.md),
[`016-familyb-bigger-source.md`](design/016-familyb-bigger-source.md),
[`017-familyb-file-crud.md`](design/017-familyb-file-crud.md).

## Phase 16 - Stretch Goals

Goal: spend the remaining practical headroom on high-value features and
hardware polish.

Status: **closed**.

Rationale: by this phase the main architecture was in place, but
there were still a few features and hardware paths that materially
improved the v1.0 experience. The rule was simple: features had to fit the
right binary family without reopening the core design.

What shipped:

- Family B language/runtime additions: array `for-in`, `switch`,
  `random(in:)`, `wait`, `tone`, `abs`, `sgn`, `hasPrefix`, and
  `hasSuffix`.
- Paged bytecode for aux and Saturn compiler/runner tiers.
- II+ Videx 80-column output for supported REPL/program paths.
- //e REPL line-history recall.
- //e REPL function redefinition.
- Compile-time errors for over-length identifiers on every binary.
- Launcher/editor/debug/test-runner polish needed for the release path.

Related design references:
[`013-80col-text.md`](design/013-80col-text.md),
[`018-on-target-test-harness.md`](design/018-on-target-test-harness.md),
[`019-keyboard-dispatch-extraction.md`](design/019-keyboard-dispatch-extraction.md).

## Phase 17 - Polish and Ship v1.0

Goal: no new features; finish documentation, distribution, and the
v1.0.0 tag.

Status: **complete**.

Rationale: v1.0 is a release-quality checkpoint, not another feature
phase. The project has enough surface area now that documentation,
repeatable verification, and packaged disk images are the work.

Completed:

- [x] Step-by-step acceptance-test playbooks:
      [`TESTING-iiplus.md`](../testing/TESTING-iiplus.md) and
      [`TESTING-emulators.md`](../testing/TESTING-emulators.md).
- [x] User tutorial:
      [`TUTORIAL.md`](../using/TUTORIAL.md).
- [x] Developer getting-started guide:
      [`DEVELOPING.md`](DEVELOPING.md).
- [x] Feature/cost table:
      [`FEATURES.md`](../using/FEATURES.md).
- [x] Showcase demo curation in [`README.md`](../../README.md).
- [x] Additional editor host tests.
- [x] On-target auto-test harness implementation and emulator automation.
- [x] Keyboard-test matrix and host coverage for the extracted pieces that
      fit the budget.
- [x] **Documentation hygiene.** Reconciled stale names, commands,
      feature lists, transcripts, and formatting across `docs/`, README
      files, on-disk Help text, `AGENTS.md`, and `CLAUDE.md`. Audited
      link integrity (55 files, 0 broken), `make`-target references,
      version strings, REPL banners, and meta-commands against the
      build; fixed a stale "5-disk set" heading in the on-disk REPL Help.
- [x] **v1.0 version cut.** Set the final v1.0 version (`1.0.0`) in the
      single-source [`version.h`](../../src/common/version.h), so the REPL
      banners and the `make release` folder name pick it up. The `v1.0.0`
      git tag is created manually on GitHub, not from this tree.
- [x] **Distribution disks.** Built and staged the eight-disk release set
      with `make release` under [`releases/v1.0.0/`](../../releases/v1.0.0/):
      four REPL program disks, one data disk, and three Family B compiler
      disks (II+, //e aux, II+ Saturn). v1.0.1 added the //e non-aux
      compiler disk and expanded the release set to nine images.
