# PROJECT_LAYOUT.md

The SwiftII repository layout. Files marked `(placeholder)` exist as
one-paragraph stubs not yet filled in - a module isn't "done" until its
stub has real content and the Makefile compiles it. The placeholder
pattern is documented in [`Makefile`](../../Makefile) section "Source lists".

```
swiftii/
├── README.md                  ← project intro, quick start, build instructions
├── AGENTS.md                  ← AI assistant + human collaborator orientation
├── CLAUDE.md                  ← two-line redirect to AGENTS.md (auto-loaded by Claude Code)
├── LICENSE                    ← MIT
├── Makefile                   ← top-level orchestrator
├── .gitignore
├── .editorconfig              ← 2-space indent, LF line endings
├── .clang-format              ← style enforcement
│
├── docs/                      ← all the .md files you're reading, by AUDIENCE
│   ├── README.md              ← doc index (routes by audience)
│   ├── using/                 ← people USING SwiftII (write & run programs)
│   │   ├── TUTORIAL.md           ← hands-on user guide
│   │   ├── LANGUAGE.md           ← the Swift subset spec
│   │   ├── API.md               ← built-in / type reference (signatures, platforms)
│   │   ├── CHEATSHEET.md         ← one-page quick reference
│   │   └── FEATURES.md           ← feature/cost table
│   ├── contributing/          ← people WORKING ON the codebase
│   │   ├── ARCHITECTURE.md       ← how it all fits together (start here)
│   │   ├── DEVELOPING.md         ← new-contributor getting started + dev loop
│   │   ├── BUILDING.md           ← toolchain setup
│   │   ├── CONSTRAINTS.md        ← hard platform/toolchain limits
│   │   ├── STYLE.md              ← coding style (C90/C17, naming, comments)
│   │   ├── PROJECT_LAYOUT.md     ← this file
│   │   ├── MEMORY_MAP.md         ← zero-page, RAM, language card
│   │   ├── OPCODES.md            ← bytecode reference
│   │   ├── SWB.md                ← .swb compiled-program file format
│   │   ├── ROADMAP.md            ← milestones and what to work on next
│   │   ├── ROADMAP-MAYBE.md      ← deferred / maybe-never ideas
│   │   ├── LESSONS.md            ← running log of gotchas
│   │   └── design/               ← numbered design-decision records
│   │       ├── 001-template.md
│   │       ├── 002-heap-and-strings.md     ← heap allocator
│   │       ├── 003-apple2-input-method.md  ← //+ source-level digraphs
│   │       └── 004…021-*.md                ← one per decision
│   └── testing/               ← TESTERS: test-layer reference + on-target playbooks
│       ├── TESTING.md            ← test layers and how to add tests
│       ├── TESTING-emulators.md  ← emulator acceptance pass (all disks)
│       ├── TESTING-iiplus.md     ← physical II+ acceptance gate
│       └── TESTING-keyboard.md   ← key-by-key shortcut matrix
│
├── src/                       ← all production code
│   ├── common/                ← shared headers (+ a couple of shared .c)
│   │   ├── config.h           ← compile-time constants (limits, sizes)
│   │   ├── version.h          ← SWIFTII_VERSION / _YEAR / _COPYRIGHT
│   │   │                        (single source of truth; make release reads it)
│   │   ├── errors.h           ← shared error codes
│   │   ├── types.h            ← uint*_t aliases, opaque type defs
│   │   ├── ctype.h            ← static type-tracker codes (CT_*; design 009)
│   │   ├── zeropage.h         ← zero-page symbol declarations
│   │   └── aux_store.{c,h}    ← shared //e aux-RAM bytecode store (Family B paging)
│   │
│   ├── lexer/                 ← portable, host- and target-compilable
│   │   ├── lexer.c            ← canonical bytes only (input layer
│   │   │                        upstream handles //+ translation)
│   │   ├── lexer.h
│   │   ├── tokens.h           ← token type enum
│   │   └── keywords.c         ← keyword table (exact lowercase match;
│   │                            input arrives canonical per 003 rev 3)
│   │
│   ├── compiler/              ← single-pass parser → bytecode (no AST)
│   │   ├── compiler.c / .h    ← top-level orchestration
│   │   ├── parser.h           ← internal Parser struct shared by all units
│   │   ├── pratt.c            ← Pratt expression parser
│   │   ├── statements.c       ← statement-level parsing
│   │   ├── strings.c          ← string-literal sub-lexer + escapes + interpolation
│   │   ├── builtin_calls.c    ← builtin / platform-call recognition
│   │   ├── emit.c             ← bytecode emission helpers
│   │   ├── bcbuf.{c,h}        ← bytecode output buffer (aux append-flush on //e)
│   │   ├── globals.{c,h}      ← global symbol table (shared REPL/file)
│   │   ├── locals.{c,h}       ← local-variable scoping
│   │   ├── loops.{c,h}        ← while / for-in / break codegen
│   │   ├── funcs.{c,h}        ← function table + call resolution
│   │   ├── types.c           ← static type tracker (ctype propagation)
│   │   └── srcwin.{c,h}       ← streaming source window (Family B, doc 016)
│   │
│   ├── vm/                    ← bytecode interpreter
│   │   ├── vm.c / .h          ← C dispatch loop (host + target)
│   │   ├── opcodes.h          ← OP_* macros (mirrors OPCODES.md)
│   │   ├── builtins_xlc.{c,h} ← extras builtins (XLC / aux copy-down bodies)
│   │   ├── bcwin.{c,h}        ← bytecode read window (//e aux paging, WITH_AUX_BC)
│   │   ├── profile.{c,h}      ← optional opcode profiler (host/dev)
│   │   ├── zeropage.s         ← zero-page reservations (apple2)
│   │   ├── zeropage_host.c    ← host stand-in for the same symbols
│   │   ├── dispatch.s         ← (placeholder) future hand-tuned 6502 dispatch
│   │   └── ops/               ← per-opcode implementations
│   │       ├── arith.c        ← OP_ADD/SUB/MUL/DIV/MOD/NEG/INC/DEC (only ops/ file compiled)
│   │       ├── calls.c        ← (placeholder; live impl is in vm.c)
│   │       ├── control.c      ← (placeholder; live impl is in vm.c)
│   │       ├── memory.c       ← (placeholder)
│   │       └── strings.c      ← (placeholder; live impl is in vm.c)
│   │
│   ├── runtime/               ← heap, refcounting, values, builtins
│   │   ├── heap.{c,h}         ← bump allocator + LIFO reclaim
│   │   ├── string_pool.{c,h}  ← static string pool (RODATA-resident)
│   │   ├── value.{c,h}        ← tagged-value ops
│   │   ├── array.{c,h}        ← heap array runtime
│   │   ├── builtins.{c,h}     ← print, readLine, min/max, …
│   │   ├── file_io.{c,h}      ← readFile/writeFile/… (Family B Runner)
│   │   ├── prodos.{c,h}       ← raw ProDOS MLI file I/O (C wrappers over mli.s)
│   │   ├── string.c           ← (placeholder; strings live on the heap today)
│   │   └── refcount.c         ← (placeholder; refcounting implicit in vm.c, design 002)
│   │
│   ├── swb/                   ← the .swb compiled-program format (see SWB.md)
│   │   ├── swb.{c,h}          ← write-side serializer (Compiler links it)
│   │   └── swb_read.c         ← read-side deserializer (Runner links it)
│   │
│   ├── repl/                  ← REPL execution mode
│   │   ├── repl.c             ← main read-compile-eval loop
│   │   ├── repl.h
│   │   ├── metacmds.c         ← :help, :list, :mem, :reset, :quit
│   │   └── metacmds.h
│   │                            (multi-line input handled by the
│   │                            launcher editor; scratch-bytecode
│   │                            management handled by bcbuf arena)
│   │
│   ├── file_runner/           ← file execution mode
│   │   ├── file_runner.c      ← open, compile fully, execute
│   │   ├── file_runner.h
│   │   └── source_stream.c    ← (placeholder; Family B streams via compiler/srcwin.c)
│   │
│   ├── editor/                ← editor compiled INTO the boot
│   │   │                        launcher (no standalone binary). MAIN-only so
│   │   │                        MLI stays live for file I/O + chaining. Host-tested.
│   │   ├── gapbuf.{c,h}       ← gap buffer (text storage)
│   │   ├── textnav.{c,h}      ← logical-line queries (start/end/up/down)
│   │   ├── screen.{c,h}       ← pure 24x40 render model (display-width, scroll)
│   │   ├── keymap.{c,h}       ← pure key → buffer dispatch
│   │   ├── fileio.{c,h}       ← load/save .swift (raw MLI on target, stdio on host)
│   │   ├── editor.c / .h      ← main loop: read key → dispatch → blit
│   │   └── editor_asm.s       ← editor asm helpers
│   │
│   ├── main/                  ← entry points
│   │   ├── main.c             ← combined interpreter: chooses REPL vs file
│   │   ├── compiler_main.c    ← Family B Compiler entry (.swift → .swb)
│   │   └── runner_main.c      ← Family B Runner entry (run a .swb)
│   │
│   └── platform/              ← target-specific I/O behind portable iface
│       ├── platform.h         ← the interface every backend implements
│       ├── host/              ← Mac/Linux for tests
│       │   ├── io.c           ← stdio-backed file & console
│       │   ├── keyboard.c     ← stdin
│       │   └── osdetect.c     ← host capability stub
│       └── apple2/            ← real target (ProDOS 2.4.3)
│           ├── screen.c       ← text output (direct video RAM writes;
│           │                    pre-IIe case render: capitals inverse,
│           │                    lowercase normal + `{`/`}`/`|` digraphs;
│           │                    //e 80-col firmware path, WITH_80COL)
│           ├── keyboard.c     ← KBD/KBDSTRB + destructive backspace
│           ├── input.{c,h}    ← input layer (auto-lowercase + apostrophe
│           │                    case marker + Ctrl-W for `_` + C-standard
│           │                    digraphs; per 003 rev 3)
│           ├── histring.{c,h} ← REPL line-history ring (//e up/down recall)
│           ├── osdetect.c     ← machine-type capability struct;
│           │                    conservative pre-IIe default, NO runtime
│           │                    $FBB3 probe (render choice = WITH_IIE flag)
│           ├── osdetect.h
│           ├── crt0_ibasic.s  ← custom cc65 startup (LC copy via
│           │                    _memcpy so the original ][ boots)
│           ├── mli.s          ← `mli(params, cmd)` ProDOS MLI trampoline
│           │                    (C wrappers live in src/runtime/prodos.c)
│           ├── chain.s        ← `chain_exec`: MLI-READ a SYS file over
│           │                    $2000 + JMP (ZP bouncer; Family B chaining)
│           ├── xlc.s / xlc_table.s ← SWIFTSAT Saturn bank-1 XLC dispatch
│           ├── aux_xlc.s / aux_table.s ← SWIFTAUX aux copy-down dispatch
│           ├── saturn_bc.s   ← Saturn-bank bytecode paging (Family B Tier 2)
│           ├── aux_bc.s      ← //e aux bytecode paging (Family B Tier 3)
│           ├── swiftii-system.cfg  ← ld65 config, lite interpreter SYS
│           ├── swiftsat-system.cfg ← ld65 config, SWIFTSAT (+XLC region)
│           ├── swiftaux-system.cfg ← ld65 config, SWIFTAUX (+staging)
│           ├── swiftii-compiler.cfg ← Family B Compiler (MAIN-only,
│           │                    LC code-name → run=MAIN; doc 015)
│           └── swiftii-runner.cfg  ← Family B Runner (MAIN-only)
│
├── stdlib/                    ← reserved future Swift-side standard library
│   ├── core.swift             ← future startup declarations
│   ├── prelude.swift          ← future user-facing library surface
│   └── README.md
│
├── tests/                     ← see testing/TESTING.md for the testing model
│   ├── unit/                  ← host-side C unit tests (clang); runner.c finds them
│   │   ├── lexer_test.c · compiler_test.c · vm_test.c · heap_test.c · value_test.c
│   │   ├── srcwin_test.c · swb_test.c · file_io_test.c · osdetect_test.c
│   │   ├── input_translate_test.c · error_paths_test.c
│   │   ├── compiler_paged_test.c · paged_runner_test.c   ← //e aux paging
│   │   └── runner.c           ← discovers and runs all test fns
│   ├── editor/                ← editor host tests (gapbuf/textnav/screen/keymap/fileio/session)
│   ├── platform/              ← histring_test.c (REPL line-history ring)
│   ├── integration/           ← .swift programs in file mode (001…028, 800) + runner.sh
│   ├── repl/                  ← scripted REPL sessions (001…020) + runner.sh
│   ├── repl-iie/              ← //e-only REPL sessions (e.g. function redefinition)
│   ├── ondisk/                ← host-runs the datadisk/tests/ suites (runner.sh,
│   │                            `make ondisk-host`; core + fbtests, emulator-free)
│   ├── sim/                   ← py65 / 6502 simulator tests (+ runner.py)
│   ├── bench/                 ← .swift micro-benchmarks
│   └── fixtures/
│       └── sample_programs/   ← (empty; .gitkeep)
│
├── tools/                     ← utilities, split by WHERE they run
│   ├── apple2/                ← ProDOS SYS programs that run ON the Apple II
│   │   │                        (cc65-built; shipped on the disks)
│   │   ├── boot_launcher/     ← the ProDOS-auto-launched boot selector
│   │   │   │                    (includes the in-process editor). One source,
│   │   │   │                    three per-disk builds: build/boot_launcher/SWIFTII
│   │   │   │                    (II+), iie/SWIFTII (-DLITE_IIE), sat/SWIFTII
│   │   │   │                    (-DFAMILYB_SATURN, Saturn-compiler-disk banner).
│   │   │   ├── boot_launcher.c
│   │   │   ├── boot_launcher_asm.s
│   │   │   └── boot_launcher.cfg
│   │   ├── debug_sys/         ← DEBUG.SYSTEM: standalone hardware diagnostic
│   │   │   │                    chained from the launcher's Debug menu.
│   │   │   │                    3 arrow-paged screens: volumes (disk space),
│   │   │   │                    detection (incl. AUX RAM), slots.
│   │   │   ├── debug.c        ← `make debug-sys` → build/debug_sys/DEBUG.SYSTEM
│   │   │   └── debug_asm.s    ← ROM + read-only MLI wrappers + probes + chain-back
│   │   │                        bouncer (no launcher link)
│   │   └── testrun_sys/       ← TESTRUN.SYSTEM: on-target auto-test sequencer
│   │                            (design 018; ships on the data disk)
│   └── host/                  ← tools that run on the DEV MACHINE
│       ├── acceptance/        ← automated emulator acceptance harness
│       │   └── run_acceptance.py  ← `make acceptance` (drives izapple2 headless)
│       ├── disasm/            ← bytecode (.swb) disassembler
│       │   └── disasm.c       ← `make disasm FILE=...`
│       ├── swbc/              ← host `.swift` → `.swb` cross-compiler
│       │   └── swbc.c         ← `make swbc`; links the same compiler core
│       ├── diskimg/           ← disk-image build scripts (driven by `make disks`)
│       │   ├── build_po.sh    ← bootable SYSTEM .po (launcher + interpreters)
│       │   ├── build_data_po.sh   ← non-boot DATA .po (samples + tests)
│       │   ├── check_readme.sh    ← validates the on-disk README sources
│       │   ├── pack_swiftsat.py   ← packs the SWIFTSAT Saturn-bank XLC overlay
│       │   ├── pack_swiftaux.py   ← packs the SWIFTAUX //e-aux copy-down directory
│       │   └── prodos243/     ← ProDOS_2_4_3.po boot template (downloaded
│       │                        by setup.sh; not committed)
│       ├── screenshots/       ← doc screenshot capture helpers
│       │   └── capture.py     ← drives izapple2 headless for screenshots
│       └── AppleCommander-ac.jar  ← disk-image tool (downloaded by setup.sh;
│                                    not committed)
│
├── emulator/                  ← emulator launch helpers (not the emulator)
│   ├── run.sh                 ← default: launch Mariani with disk
│   ├── run_izapple2.sh        ← launch izapple2 (the run-iz-* targets)
│   ├── run_repl.sh            ← launch in REPL mode
│   ├── run_file.sh            ← launch and immediately run a file
│   └── README.md
│
├── build/                     ← all build output, gitignored
│   ├── host/                  ← clang artifacts
│   │   ├── obj/
│   │   ├── swiftii_host       ← host build of the interpreter
│   │   ├── disasm
│   │   └── unit_tests
│   ├── apple2/                ← cc65 artifacts
│   │   ├── obj/
│   │   ├── SWIFTIIP.SYSTEM    ← II+ lite (II+/earlier, //+ typing model)
│   │   ├── iie/SWIFTIIE.SYSTEM ← //e lite (WITH_IIE: native case+lowercase)
│   │   ├── swiftsat/SWIFTSAT.SYSTEM ← Saturn 128K extras
│   │   ├── swiftaux/SWIFTAUX.SYSTEM ← //e aux extras
│   │   │                        (no unified SWIFTIIX.SYSTEM; the two
│   │   │                         split the extras role per machine)
│   │   ├── compiler/COMPILER.SYSTEM ← Family B compiler
│   │   ├── runner/RUNNER.SYSTEM     ← Family B runner, II+ build
│   │   ├── runner/iie/RUNNER.SYSTEM ← Family B runner, //e build
│   │   │                        (no HGR variants SWIFTIIH/SWIFTIIF -
│   │   │                         ROADMAP Maybe item 1; the editor lives
│   │   │                         in the boot launcher, no standalone
│   │   │                         SWIFTED.SYSTEM)
│   │   ├── swiftiip.map       ← linker map for size analysis
│   │   └── swiftiip.lbl       ← symbol labels for disassembler
│   ├── boot_launcher/             ← boot selector (+ in-process editor)
│   │   └── SWIFTII            ← installed on .po as SWIFTII.SYSTEM,
│   │                            the ProDOS-auto-launched entry point
│   └── disk/                  ← nine-disk set (make disks); 1 interpreter per disk
│       ├── swiftii-iip-lite-repl.po ← II+ launcher + SWIFTIIP
│       ├── swiftii-iip-sat-repl.po  ← II+ launcher + SWIFTSAT
│       ├── swiftii-iie-lite-repl.po ← //e launcher + SWIFTIIE
│       ├── swiftii-iie-aux-repl.po  ← //e launcher + SWIFTAUX
│       ├── swiftii-data.po    ← non-boot DATA image (samples + tests)
│       ├── swiftii-iip-compiler.po     ← Family B Tier-1 (II+ flat)
│       ├── swiftii-iie-compiler.po     ← Family B Tier-1 (//e-native flat, fw 80-col)
│       ├── swiftii-iie-aux-compiler.po ← Family B Tier-3 (//e aux-paged)
│       └── swiftii-iip-sat-compiler.po ← Family B Tier-2 (II+ Saturn)
│
├── releases/                  ← published disk images (committed, vs build/)
│   ├── README.md              ← the nine-disk set + how to use them
│   └── v<version>/            ← `make release` stages the 9 .po here;
│                                version from src/common/version.h; *.po
│                                gitignore-exempted (!releases/**/*.po)
│
├── progdisk/                  ← sources that ship on the PROGRAM/binary disks
│   ├── README.md             ← overview of the program-disk sources
│   ├── readme-repl.txt       ← on-disk Help -> README.TXT, the ONE canonical
│   │                            REPL source (mixed case; build folds to
│   │                            ALL-CAPS for II+ disks, like the About screen).
│   │                            `@YEAR@`/`@VERSION@` are filled from
│   │                            version.h; `@BUILT@` is the build timestamp.
│   ├── readme-compiler.txt   ← " the ONE canonical Family-B compiler source
│   ├── samples/               ← regular demos -> SAMPLES/ (run on ANY system,
│   │   └── *.swift              incl. lite REPL; ship on every program disk)
│   ├── xsamples/              ← x-prefixed EXTRAS-REPL demos -> XSAMPLES/ (ship
│   │   └── x*.swift             on extras REPL + Family B disks; gfx/sound/games)
│   └── fbsamples/             ← x-prefixed FAMILY-B-ONLY demos -> XSAMPLES/
│       └── x*.swift             (random/switch/for-in; data disk only - REPLs reject)
│
├── datadisk/                  ← sources EXCLUSIVE to the data disk
│   ├── README.md             ← overview of the data-disk-only subfolders
│   ├── xsamples/              ← oversize showcases → XSAMPLES/ (xbig, xgrdemo, xfuncs;
│   │   └── x*.swift             too big for the program-disk staging cap)
│   └── tests/                 ← self-checking tests, tiered by capability
│       │                        (each tier has its own README.md; read the "fail 0")
│       ├── core/              ← general tier → TESTS/ (any REPL); t*.swift
│       ├── xtests/            ← extras tier → XTESTS/ (SWIFTSAT/SWIFTAUX); x*.swift
│       ├── fbtests/           ← Family B tier → FBTESTS/ (compiler-runner; doc 015)
│       └── errtests/          ← error-message DEMOS → ERRTESTS/ (deliberately fail,
│                                not self-checking; cf. tests/unit/error_paths_test.c)
│
└── scripts/                   ← developer convenience
    ├── setup.sh               ← installs cc65, py65, AppleCommander,
    │                            ProDOS boot files
    ├── format.sh              ← runs clang-format on src/
    ├── new_opcode.sh          ← scaffolds a new opcode end-to-end
    └── new_test.sh            ← scaffolds a new integration test
```

---

## Notes on the structure

### `src/repl/` and `src/file_runner/` are siblings

They are the two execution-mode drivers. Both call into
`src/lexer/`, `src/compiler/`, `src/vm/`, and `src/runtime/`. Neither
calls the other. `src/main/main.c` is a small dispatcher that, based
on argc/argv, hands off to one or the other.

This split makes it easy to:

- Compile the host build with both modes for testing.
- Future-proof for an "execute precompiled .SWB" mode (would become a
  third sibling in `src/`).
- Disable a mode at compile time via `config.h` if we ever build a
  size-constrained variant.

### Family B: the on-disk Compiler + Runner

The "execute precompiled `.swb`" mode is **Family B**
(design doc 015): two MAIN-only tools that hand off through a `.swb` disk file.
The source involved:

- `src/main/compiler_main.c` - the **Compiler** entry: streams a `.swift`
  through a 4 KB low-RAM source window ($0C00–$1BFF, doc 016) with an
  in-place percent progress line, compiles it (lexer + `src/compiler/` +
  `src/runtime/` constant heap), writes a `.swb` next to the source,
  chains the Runner.
- `src/compiler/srcwin.{c,h}` - the **streaming source window** (doc 016
  Tier 2): slides at statement boundaries via a `WITH_SWB`-gated Parser
  refill hook, so source size is disk-bounded (the bytecode arena is the
  practical program cap).
- `src/main/runner_main.c` - the **Runner** entry: reads a `.swb`, runs it on
  the VM (`src/vm/` + `src/runtime/` + all builtins inline), no compiler.
  Ctrl-C breaks a running program (vm.c polls on OP_LOOP, `WITH_SWB`-gated).
- `src/swb/swb.{c,h}` + `swb_read.c` - the `.swb` format, split into a
  write-side TU (Compiler links it) and a read-side TU (Runner links it):
  ld65 links whole objects, so one TU would make each binary carry the
  other's half as dead code. Host tests link both.
- `src/runtime/prodos.{c,h}` + `src/platform/apple2/mli.s` - raw ProDOS MLI
  file I/O (fixed $1C00 buffer, no cc65 stdio/malloc). Used by the Compiler,
  Runner, **and the editor** (`src/editor/fileio.c`).
- `src/runtime/file_io.{c,h}` - backs the Swift `readFile`/`writeFile`
  builtins (Runner only).
- `src/platform/apple2/chain.s` - `chain_exec`: chain a SYS file over $2000
  (Compiler→Runner, and either→launcher on finish).
- `src/platform/apple2/swiftii-compiler.cfg` / `swiftii-runner.cfg` - MAIN-only
  link configs (LC code-name → MAIN, empty LC so ProDOS MLI survives).

These are Family-B-only (gated `WITH_SWB`) and do **not** link into the Family
A interpreters, which stay at the MAIN ceiling.

### `src/main/` is tiny on purpose

It does only:

1. Look at argc/argv (or, on Apple II, the BASIC `0/...` parameter).
2. Initialize platform.
3. Initialize VM and runtime.
4. Call `repl_run()` or `file_runner_run(path)`.
5. Tear down and return.

If `src/main/main.c` exceeds 200 lines, code has leaked from one of
its callees into it and should be moved back.

### `src/common/` is for what's truly shared

- Type definitions used by 3+ modules.
- Configuration constants.
- Zero-page symbol declarations.

It is **not** for utilities. Utilities go with the module that uses
them. Sharing-by-default is how a project ends up with a 4000-line
"util.c" that everything depends on.

### Placeholders are a deliberate convention

Many `.c`/`.s` files contain only a one-paragraph comment describing
what will fill them in. They live on disk so the directory
structure matches the intended end state and so future work has an
obvious home for its code. They are **not** in the Makefile's source
lists - adding a placeholder to the build is the explicit signal that
the module has real content. See the comment in
[`Makefile`](../../Makefile) above `CORE_SRC`.

### `src/vm/dispatch.s` and `src/vm/ops/*.c`

These directories exist for an eventual split, when the VM dispatch
loop moves to hand-tuned 6502 and per-opcode implementations are
large enough to warrant individual files. Today, the live dispatch is
in [`src/vm/vm.c`](../../src/vm/vm.c) with arithmetic helpers in
[`src/vm/ops/arith.c`](../../src/vm/ops/arith.c) (the only `ops/` file
currently compiled). The other `ops/*.c` files are placeholders.

### `stdlib/` exists from day one

Even though nothing loads Swift code at startup today, the directory
is there so that when we eventually move `print` (or some other
builtin) from C into Swift, there's an obvious place for it.

### `build/` is fully disposable

Anything in `build/` can be regenerated from `src/` and `tools/` with
a `make clean && make`. Don't commit anything from `build/`. The one
committed built artifact is a published disk-image set: `make release`
copies the nine `.po` from `build/disk/` into `releases/v<version>/`,
which is gitignore-exempted (`!releases/**/*.po`) so a tagged version is
committed. Everything else under `build/` stays disposable.

### `docs/` is the only place for documentation

There are no per-module `README.md` files. Module-level documentation
lives in the file header comment of the module's main `.c` file. This
prevents documentation drift between the README and the code.

### Downloaded artifacts (not committed)

`scripts/setup.sh` fetches two things that are NOT in the repo for
license-clarity reasons:

- `tools/host/AppleCommander-ac.jar` - disk-image tool
- `tools/host/diskimg/prodos243/ProDOS_2_4_3.po` - the bootable ProDOS 2.4.3
  image `build_po.sh` uses as a disk template

Both are listed in `.gitignore`. The Makefile expects them at the
paths shown above; override via env vars if you stash them elsewhere
(see [`BUILDING.md`](BUILDING.md)).

### What's not here

- A `vendor/` or `third_party/` directory. We have no third-party
  dependencies in `src/` and shouldn't acquire any.
- An `include/` directory. Headers live next to their `.c` files in
  the same module directory.
- A `lib/` or `bin/` directory. Build outputs are in `build/`.
