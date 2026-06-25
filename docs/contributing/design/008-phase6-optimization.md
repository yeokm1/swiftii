# 008 - Phase 6 optimisation and size reduction

## Problem

The Phase 5 close binary is **40,550 B against a 40,704 B ProDOS-SYS
load ceiling** - 154 B of headroom. Phase 7 (Tier 2: type tracker,
`switch`, `struct`, more builtins, array methods, subscript-set,
`if let` else) will add several KB of compiler and VM code on top.
At today's headroom the next non-trivial Phase 7 commit overflows the
SYS image before it even reaches the cc65 size budget for CODE.

`ROADMAP.md section Phase 6` asks for **20% headroom in every section**
before Phase 7 starts. Today's actual fill is:

| Section | Now | 28 KB / 10 KB / 40 KB target | Headroom now | Headroom @ 20% target |
|---------|-----|-------------------------------|--------------|-----------------------|
| Binary file | 40,550 B | 40,704 B | 154 B (0.4%) | 8,141 B → binary ≤ 32,563 B |
| CODE | 27,847 B | 28,672 B (28 KB) | 825 B (2.9%) | 5,734 B → CODE ≤ 22,938 B |
| LC | 10,106 B | 10,240 B | 134 B (1.3%) | 2,048 B → LC ≤ 8,192 B |
| BSS | ~6.2 KB | $B700−$95E4 = ~8.8 KB | 2,275 B (26%) | already past target |

Binary file is the binding constraint. LC bytes are file-resident
(`load = MAIN, run = LC`), so cuts in either CODE or LC shrink the
file one-for-one; they cannot help by being shuffled between
segments. The real Phase 6 budget ask is "claw back ≥ 8 KB of
compiled instructions / RODATA from the SYS file".

A secondary problem: REPL prompt-to-prompt latency for a trivial
statement (`let x = 1`) needs to clear 750 ms on real hardware (a loose
goal, not a hard ceiling). There is no baseline number
in tree today, only the host clock - Phase 6's first job is to
measure where time is actually being spent before deciding which
hot paths to rewrite.

A tertiary problem: zero page is unused by SwiftII today (per
`MEMORY_MAP.md section Zero page`). The hot variables in `vm.c` (PC,
software stack pointer, dispatch scratch) are linker-placed in BSS
and accessed via 16-bit absolute addressing. Moving them to ZP is the
biggest single per-opcode cycle saving available on 6502, but the
claim has to be made carefully - ProDOS, the cc65 runtime, and any
co-resident tooling all share the page.

## Proposal

Phase 6 ships as **eight commits**, ordered low-risk-first so each
commit closes with a green tree and a measurable size/perf delta.
The dispatch-loop assembly rewrite - the largest single intervention -
lands in the middle of the sequence, after the measurement and
low-risk size-sweep commits have already restored binary headroom and
established a baseline.

```
commit 1: Phase 6 design doc (this file).         no code; alignment.
commit 2: profiling infrastructure + baselines.   small CODE; numbers.
commit 3: low-risk size sweep                     binary cut: 396 B
          (-Or + register, error-string dedup).
commit 4: ZP claim + state migration              binary cut: 46 B
          (ZP $D0-$DF, vm_pc/sp/fp/op via zpsym).
commit 5: value_retain/release tightening         binary cut: ~0.5 KB
          (inline or asm).
commit 6: RODATA-to-LC sweep for remaining        moves bytes; opens
          modules (value.c, lexer.c, vm.c, etc.). main-RAM headroom.
commit 7: re-measure, lock budgets in CI,         no code; closes the
          write Phase 6 close note.               phase.
```

**Plan revision (2026-05-23, after commit 4 ZP migration):** the
original eight-commit plan had commit 4 = ZP + parity-bridge dispatch
skeleton and commit 5 = hot opcode handlers in ca65. After
measuring the cumulative wins from commits 2–4 (~442 B, with the ZP
state migration giving a clean 46 B on its own - a comfortable
positive result rather than the predicted "binary delta roughly
flat") we judged the parity-bridge skeleton too risky for its own
sake: the skeleton without hot-handler conversions probably *grows*
the binary by 300–800 B (8–12 B of save/restore per opcode stub),
and the hot-handler payoff only kicks in once several opcodes are
converted. Phase 6 closes after the smaller commits 5–7. The asm
dispatch + hot-handler conversions move to a **Phase 6b spin** if
Phase 7's feature work squeezes the binary back against the ProDOS
ceiling. The 20% headroom gate is unmet at this revision; the
deficit is documented and accepted.

Each commit is independently revertable.

## Detailed design

### Commit 2 - profiling infrastructure

Goal: produce a single-page "where do the cycles go" table for two
benchmark programs (`tests/integration/044_array_sum.swift` from
Phase 4, FizzBuzz from Phase 3) before any optimisation lands.

Build artefacts:
- `src/vm/profile.h` / `profile.c`: per-opcode dispatch counter, gated
  behind `SWIFTII_PROFILE` (compile-time macro, default off).
  Counts are `unsigned long`; aggregated by opcode tag and dumped to
  stderr at `vm_run` exit. When off, the macro expands to nothing -
  zero overhead on the production build.
- `tests/bench/`: two `.swift` programs plus expected
  `(opcode_total, top10_opcodes)` files.
- Host-side runner `make bench` that builds with `SWIFTII_PROFILE=1`,
  runs each program, and writes the dispatch table to
  `build/host/bench/<name>.profile`.

We deliberately do **not** add a cycle counter on the cc65 side
yet - `CONSTRAINTS.md` reserves real-hardware latency measurement to
the human, and `make bench` host numbers are a proxy that lets us
rank opcodes by frequency, which is what informs the asm-rewrite
shortlist. Latency-on-target is a Phase 6 close (commit 8)
verification, not an in-commit measurement.

Output goes into `LESSONS.md` once captured; the design doc records
the methodology only.

### Commit 3 - low-risk size sweep

Three pure-C interventions. **The original plan's single-arg
`__fastcall__` sweep is dropped** based on the 2026-05-22 LESSONS
entry: cc65 at `-O -Cl` already passes the single argument in A/X
without an explicit `pushax`, so the attribute is a no-op for
single-arg functions. The pilot conversion on `lexer_next`'s 80
call sites yielded 0 B. Keep `__fastcall__` for multi-arg helpers
only, where it changes which arg goes through registers vs. the
software stack.

1. **`-Or` register-variables flag.** Set globally in the Makefile
   alongside the existing `-O -Cl`. cc65 docs claim 3–8% code-size
   reduction. Risk: cc65 has historical bugs in `-Or` codegen for
   nested loops + static-locals (which we use heavily). Mitigation:
   run `make ci` immediately after enabling; on the first sign of a
   miscompilation, drop the flag.

   Estimated cut: 2–5% of CODE = 600–1,400 B.

2. **Error-message dedup.** Today's parser and runtime emit ~80
   distinct error strings. A pass that:
   - Strips the trailing `\n` from RODATA strings, prints it from the
     error formatter instead.
   - Folds repeated phrases (`"expected "`, `"after "`, `"in "`,
     `"identifier"`, `"expression"`) into a small handful of fragment
     constants the formatter concatenates.
   - Drops `"too many "` prefix collisions (`"too many globals"`,
     `"too many funcs"`, `"too many locals"` share the prefix).

   Note the 2026-05-22 lesson on cc65's intra-TU literal dedup:
   identical string literals inside one .c file are already
   coalesced. The win here is cross-TU dedup, where today each
   .c file pays its own copy of common phrases.

   Estimated cut: 300–600 B RODATA.

3. **Multi-arg `__fastcall__` (limited).** Apply only to the
   hottest 3–4 multi-arg helpers identified by commit 2's profile:
   no single-arg conversions. Candidate set TBD after grepping
   2-arg+ hot-path callees.

   Estimated cut: 50–200 B.

Commit 3 acceptance: `make size` shows binary ≤ 39,500 B (≥ 1 KB
cut, lower than the original 1.5 KB target after the single-arg
`__fastcall__` drop), `make ci` green. The shortfall is absorbed
by commits 5/6.

### Commit 4 - ZP claim + state migration to globals

**Revised after measurement (2026-05-23).** The original plan
combined the ZP claim with a full ca65 dispatch skeleton + parity
bridge. After commits 2–3 closed with the cc65 optimiser producing
better codegen than the design doc assumed (the `__fastcall__` lever
turned out to be a no-op at `-O -Cl`), we revised commit 4 to do
only the *infrastructure half* of the original plan. The asm
dispatch skeleton and the hot-handler conversions move to a
**Phase 6b spin** if and when Phase 7's feature work pressures the
binary back against the ProDOS ceiling.

**Zero-page claim.** Take **$D0–$DF (16 bytes)**, leaving $E0–$EF
reserved for lexer hot variables in a future commit (which may or
may not land - the lexer is not on the shortlist). The claim is
below the cc65 runtime's upper-page usage but above what ProDOS
reserves.

| Address | Name | Type | Purpose |
|---------|------|------|---------|
| $D0–$D1 | `vm_pc` | u16 | VM program counter |
| $D2 | `vm_sp` | u8 | VM software stack pointer |
| $D3 | `vm_fp` | u8 | VM frame pointer |
| $D4–$D5 | `vm_tmp1` | u16 | scratch pointer 1 |
| $D6–$D7 | `vm_tmp2` | u16 | scratch pointer 2 |
| $D8–$D9 | `heap_ptr` | u16 | heap bump pointer mirror |
| $DA | `vm_op` | u8 | current opcode |
| $DB | `vm_flags` | u8 | dispatch flags |
| $DC–$DD | `vm_tmp3` | u16 | call-frame scratch |
| $DE–$DF | (spare) | - | 2 bytes free |

Layout differs from the original design doc - `vm_sp`/`vm_fp` are
single-byte (the VM has ≤256 stack slots) rather than 2 bytes each,
reclaiming the bytes for a 2-byte spare and an extra scratch slot.

`src/common/zeropage.h` declares all of these via `#pragma zpsym`
for cc65 and as ordinary externs on the host. Storage:
`src/vm/zeropage.s` provisions the ZP slots on cc65;
`src/vm/zeropage_host.c` defines matching globals on the host. The
ld65 config gets a new `SWIFTIIZP` segment at `$00D0` (size `$10`).
`MEMORY_MAP.md section Zero page` is updated to mark these slots claimed.

**State migration.** `vm_run`'s former locals (`pc`, `sp`, `fp`,
`op`) are renamed to `vm_pc`, `vm_sp`, `vm_fp`, `vm_op` and resolve
to the ZP globals. cc65's ZP-addressing codegen for these is at
least as compact as the previous `register` locals (the regbank ZP
shuffle on function entry/exit is gone), netting a 46 B reduction.
The host build sees the same names as ordinary globals; behaviour
is identical to the pre-commit-4 C path.

Commit 4 acceptance:
- `make ci` green (test + sim + integration + repl + apple2 + size).
- `make size` shows binary ≤ 40,108 B (≥ 442 B cumulative Phase 6
  cut from the 40,550 B Phase 5 close).
- The asm dispatch skeleton (`dispatch.s` jump table + parity bridge
  + handler JMPs) is **deferred** to Phase 6b. The Phase 6 close
  proceeds without it.

### Commit 5 - value_retain/release tightening

`value_retain` and `value_release` are called on every push and pop
of a heap-bearing value (strings, arrays, future structs). The fast
path is "tag is `T_NIL`/`T_BOOL`/`T_INT` → no-op"; the slow path is a
heap refcount touch. The current cc65 codegen for the C
implementation pushes the value pointer to the software stack and
function-calls through.

Commit 5 either:
- (a) Inlines the fast path at every emit site (cc65 honours `static
  inline` poorly; `__fastcall__` plus link-time inlining via
  small-function heuristics gets ~70% of the win), or
- (b) Rewrites both functions in ca65 with a 4-line tag check + JSR
  through to a C slow path on heap objects.

Choice picked during the commit, after measuring. Either approach
should save 400–800 B CODE.

### Commit 6 - RODATA-to-LC sweep

The Phase 5 prep commit moved `pratt.c`'s RODATA to LC via a
per-file Makefile rule (see `MEMORY_MAP.md section Phase 5 prep`). Commit
6 generalises this:

- Modules with non-hot RODATA (`statements.c`, `repl.c`,
  `metacmds.c`, `builtins.c`, `lexer.c` keyword table, parser error
  strings) get the `--rodata-name LC` flag in the Makefile.
- The `MEMORY_MAP.md section Phase 5 prep` section is renamed and broadened
  to "RODATA → LC migration" and lists every per-file rule.
- The lexer keyword table specifically is a candidate for `LC RODATA`
  even though the lexer itself stays in CODE - the table is read
  once per token, the cross-segment access is fine, and the table is
  ~200 B that wants moving.

Commit 6 does **not** reduce binary file size by itself (per the
2026-05-22 LESSONS entry). It buys main-RAM BSS headroom that Phase 7
will need for the type-tracker tables and the `switch`/`struct`
scratch buffers.

### Commit 7 - phase close

- Re-run `make bench` and capture the closing dispatch table +
  per-opcode cycles.
- Tighten `make size` budgets in `tools/sizecheck.py` (or wherever
  the budget thresholds live) to lock in the won bytes plus a small
  margin. If the 20% gate is met across the board, set the gates at
  20%; if not, set them at "today + 1 KB margin" and note Phase 6b
  as a candidate.
- Add a `MEMORY_MAP.md section Phase 6 close` section mirroring the format
  of the Phase 4 entry (load image, BSS layout, LC layout).
- Memory note for the persistent index: Phase 6 closed on YYYY-MM-DD
  with binary X B (Y B headroom), CODE Z B, LC W B.

### Phase 6b (deferred) - asm dispatch + hot opcode handlers

Pulled out of Phase 6 commits 4–5 when the in-flight measurement
showed the cc65 optimiser was already delivering most of what the
asm rewrite promised, and the parity-bridge save/restore overhead
on stubbed handlers would temporarily *grow* the binary. Phase 6b
is the plan when Phase 7's feature work pushes the binary back
against the ProDOS ceiling.

Tasks (in order):

1. Extract every `switch` case in `vm_run` into a standalone C
   handler function that reads/writes the ZP globals claimed by
   Phase 6 commit 4. This is mostly mechanical - 65 cases, no
   semantic change. Host build still calls them from the switch.
2. Write `src/vm/dispatch.s` with a 65-entry word jump table and
   the fetch-decode-jump loop sketched earlier in this doc. Every
   handler is initially a parity-bridge stub: save ZP to BSS, JSR
   to the C handler from step 1, reload ZP, JMP `vm_dispatch`.
   The cc65 build's `vm_run` becomes a thin wrapper that JSRs into
   `vm_dispatch`.
3. Convert hot opcodes to native ca65 handlers in `dispatch.s`,
   bypassing the parity bridge. Shortlist from `make bench`
   measurements (in decreasing dispatch frequency):
   - `OP_GET_GLOBAL` (33% on loop, 19% on fizzbuzz)
   - `OP_SET_GLOBAL` (17% on loop)
   - `OP_JUMP_IF_FALSE` (13% fizzbuzz, 8% loop)
   - `OP_INT_U8` (19% fizzbuzz)
   - `OP_OVER` / `OP_LE` (8% each on loop - the `for ...` header)
   - `OP_LOOP` (8% on loop)
   - `OP_ADD` / `OP_INC` (8% each on loop)
   - `OP_EQ` / `OP_MOD` (10% each on fizzbuzz)
   `OP_GET_LOCAL` / `OP_SET_LOCAL` are absent from both benchmarks
   because the programs use top-level (global) vars. A function-loop
   benchmark would surface them; add when needed.
4. Validate each conversion against the host build (which remains
   the canonical correctness oracle).

Expected delta: cumulative 1–2 KB binary cut after step 3 on
~10 opcodes; further cuts available by extending the shortlist.

### Edits to existing docs

- `MEMORY_MAP.md section Zero page` - mark $D0–$DF claimed.
- `MEMORY_MAP.md section Phase 4 actual layout` - added "Phase 6
  optimisation close" section.
- `OPCODES.md` - note that selected opcodes have ca65 handlers in
  `src/vm/dispatch.s` (no behavioural change).
- `BUILDING.md` - document `SWIFTII_PROFILE`, `make bench`.
- `LESSONS.md` - capture per-commit deltas as they land.
- `LESSONS.md` - record the Phase 6 close latency number once measured
  on real hardware.

## Alternatives considered

**Do nothing; raise the ProDOS load ceiling.** Not available - the
ceiling is a ProDOS-imposed hardware/OS boundary at $BF00. The only
way past it is to ship multiple SYS files (the build matrix - a lite
binary plus an extras binary, lands in Phase 8) - which Phase 7's Tier 2
features need *anyway* to gate the extras-only features behind the
extras binary. But Phase 7's
feature work needs Phase 6's headroom *before* the split, because
the language-level Tier 2 features (type tracker, `switch`, `struct`)
are not extras - they go into the base `SWIFTII.SYSTEM`.

**Rewrite the whole VM in ca65.** Estimated 4–6 weeks of work,
fragile cc65/ca65 interop on every Phase 7 opcode addition, and the
host build either bit-rots (no C oracle for new opcodes) or duplicates
every change. Rejected: the dispatch loop and ~10 hot handlers are
the load-bearing 90%; the rest is not worth the ongoing tax.

**Threaded code (direct-threaded or token-threaded dispatch).** A
real 6502 win - saves the per-opcode jump-table indirection. But it
requires every opcode emit to write a 16-bit handler address instead
of a 1-byte opcode, ~2× bytecode size. Rejected: bytecode lives in
main RAM where bytes are already scarce, and the dispatch-loop win
(JMP through ZP-resident jump table) gets us close.

**Bank switching for Tier 2 features (Phase 6 prerequisite for
Phase 7's bank-2 features).** The `$D000-$DFFF` region is double-
banked (per `CONSTRAINTS.md section Memory budget`); bank 2 is reserved
for Tier 2 features. Wiring up the bank-switch machinery now would
let Phase 7 ship more code without growing the main-RAM image.
Rejected for Phase 6: the savings are bytes-per-feature in Phase 7,
not bytes-now; commit 7 (RODATA-to-LC) buys most of what bank 1
buys without needing bank-switching infrastructure. The bank-switch
wiring lands in Phase 7 itself if and when bank 1 fills.

**Compress bytecode (LZ-ish, varint operands).** Reduces bytecode
size, not interpreter size. Bytecode is a small fraction of the
binary today and won't grow into a constraint until Phase 8+
programs. Rejected as premature.

**Move heap to aux RAM unconditionally.** Aux RAM is IIe-only, and
the lite binary targets the //+. Aux RAM is a later //e-only extras
concern (it became `SWIFTAUX` in Phase 11), not a Phase 6 lever.

**Skip the design doc, go straight to commit 2.** Rejected by the
user - the eight-commit sequence above has cross-commit dependencies
(ZP claim, parity bridge, fallback story) that benefit from being
written down before any of them lands.

## Cost

**Memory cost (CODE/RODATA/binary):** the *goal* is a net reduction.
Targets:
- Commit 3 (size sweep): -1.5 KB binary.
- Commit 5 (asm hot handlers): -2.5 KB binary.
- Commit 6 (retain/release): -0.5 KB binary.
- Commit 4/7 are roughly net-zero in binary (shuffling, parity
  bridges); they enable the cuts.
- Phase total: -4.5 KB binary minimum (target ≥ 8 KB for the 20%
  gate; the gap is the Phase 6b backlog if needed).

**Memory cost (BSS):** the ZP claim ($D0–$DF, 16 B) is moved
*out of* BSS, freeing 16 B. The host build keeps the variables as
static globals; the ZP only matters on cc65.

**Performance cost:** negative - the whole point is a speedup. The
unknown is *how much*; commit 2's baseline plus commit 8's
re-measurement settle it. CONSTRAINTS.md's 750 ms REPL-latency gate
is the hard target.

**Code complexity cost:** medium. The C ↔ ca65 dispatch boundary is
new - every Phase 7 opcode addition now has to consider whether it
lives in C-only (parity bridge) or asm (hot handler). The decision
rule lands in `OPCODES.md` after commit 5: opcodes with ≥ 1% dispatch
frequency in the benchmark profile graduate to asm; everything else
stays C. Default-to-C keeps the cost contained.

**Schedule cost:** estimated 5–8 working days of focused work.
Commit 4 (ZP + skeleton) is the longest single commit (~2 days);
the rest are 0.5–1 day each. Each commit is independently
revertable, so the risk-per-day is capped at one commit's worth.

## Migration

- **Existing bytecode is unchanged.** All 65 opcodes keep their
  numeric values and semantics. The on-disk format from Phase 5 (no
  on-disk format yet - bytecode is in-memory only) is unaffected.
- **Existing tests are unchanged.** Host unit tests stay green
  throughout; the cc65 path is exercised by `make sim` and
  `make run` per commit.
- **The REPL and file-mode CLI surfaces are unchanged.** No language
  change, no error-message change beyond commit 3's dedup (which is
  factored to preserve every error's *meaning*; the wording may
  shift by a few words).
- **Host build remains the correctness oracle.** Any divergence
  between the host C dispatch and the cc65 ca65 dispatch is a bug;
  the host build is canonical, the cc65 ca65 implementation has to
  match it byte-for-byte on every test fixture.
- **One-way doors:** the ZP claim. Once $D0–$DF is allocated, every
  future cc65 build assumes it's reserved. Co-resident tools (the
  editor, and a possible future FP wrapper into Applesoft ROM) have to
  respect the claim. The claim is documented in `MEMORY_MAP.md` so any
  later co-resident tool picks it up.

## Decision

Implemented as a revised six-commit plan (commit 1 design doc through
commit 7 phase close, commit 6 RODATA-to-LC sweep skipped).

**Closing summary (2026-05-23):**

| Commit | Description | Binary delta |
|--------|-------------|--------------|
| 1 | Design doc (this file) | - |
| 2 | Profiling infrastructure + baselines | - |
| 3 | `-Or` + `register` + cross-TU error dedup | **−396 B** |
| 4 | ZP claim ($D0–$DF) + vm_pc/sp/fp/op migration | **−46 B** |
| 5 | value_retain/release fast-path | **−5 B** |
| 6 | RODATA-to-LC sweep | **skipped** |
| 7 | Phase close (this entry) | - |

**Total: −447 B (40,550 → 40,103). Headroom 154 B → 601 B.**

The 20% headroom gate (≤ 32,563 B binary) is **not met**. The
two big-ticket items that would have closed it - ca65 dispatch
loop and hot-opcode asm conversions - are on standby in section "Phase 6b
(deferred)" and trigger only if Phase 7's feature work squeezes the
binary back against the ProDOS ceiling.

Why we stopped short of the original plan:

- Commit 3 revealed cc65's `-O -Cl` optimiser is doing more than the
  design doc assumed. The single-arg `__fastcall__` lever was a
  no-op (per the 2026-05-22 LESSONS entry); the `-Or` flag alone
  also a no-op until `register` declarations were added.
- Commit 4 confirmed the ZP migration was a clean win (46 B) but
  the predicted ca65-skeleton parity bridge would cost more than
  the switch elimination saves at this stage - the asm payoff only
  arrives after the *next* commit's hot-handler conversions, with
  no green-tree commit in between.
- Commit 5 confirmed the macro fast-path approach costs 426 B more
  than it saves. cc65's call overhead is already small enough that
  inlining a 6-byte tag check at 35 sites loses.
- Commit 6 was skipped because BSS headroom is already 2,658 B -
  comfortable for Phase 7 - and LC has only 203 B of slack to
  absorb migrated RODATA. The hedge wasn't necessary now.

Lesson recorded in `docs/contributing/LESSONS.md` (2026-05-23 entries for each
commit). The Phase 6b plan stays warm for when it's needed.

---

## Phase 6b kick-off (2026-05-24)

**Status**: paused 2026-05-24 after honest byte arithmetic; see
"Why paused" below. Bench infrastructure landed and is preserved for
the next attempt.

### Trigger

Phase 7 c8a closed at **40,559 B / 145 B headroom**. The original
Phase 8 (platform APIs, now Phase 10 after the 2026-05-24/25
restructures) first slice (15 builtins: text control + speaker + GR) is honestly
**1.8–2.4 KB** - 13–17× the available headroom in main RAM. Even the
smallest credible sub-slice (`home`/`htab`/`vtab`/`peek`/`poke` +
speaker, ~700–900 B) exceeds headroom by 5×. Phase 6b's projected
1–2 KB binary cut was the hoped-for budget to fund the first
sub-slice; the math below shows that projection was wrong.

### Refreshed bench numbers (`make bench`, 2026-05-24)

Added two new bench programs since the 2026-05-23 baseline to surface
Phase 7's call and string-conversion paths that the original two
programs did not exercise:

- `tests/bench/calls_loop.swift` - 500 iterations of a 1-arg function
  call; hits OP_CALL / OP_RETURN / OP_GET_LOCAL.
- `tests/bench/string_interp_loop.swift` - 100 iterations of
  `"n=" + String(i)`; hits OP_STR / OP_STR_INTERP_I / OP_ADD's heap
  string branch.

Top opcodes across all four programs (`%` = dispatch share within
that program):

| Opcode             | loop_sum_1000 | fizzbuzz_100 | calls_loop | string_interp |
|--------------------|--------------:|-------------:|-----------:|--------------:|
| OP_GET_GLOBAL      |       33.31%  |      18.91%  |    19.99%  |       22.97%  |
| OP_SET_GLOBAL      |       16.65%  |       3.67%  |    13.31%  |       15.21%  |
| OP_JUMP_IF_FALSE   |        8.33%  |      13.29%  |     6.67%  |        7.68%  |
| OP_OVER            |        8.33%  |       3.71%  |     6.67%  |        7.68%  |
| OP_LE              |        8.33%  |       3.71%  |     6.67%  |        7.68%  |
| OP_LOOP            |        8.32%  |       3.67%  |     6.65%  |        7.60%  |
| OP_INC             |        8.32%  |       3.67%  |     6.65%  |        7.60%  |
| OP_ADD             |        8.32%  |          -   |     6.65%  |        7.60%  |
| OP_INT_U8          |        0.02%  |      19.24%  |     6.68%  |        0.15%  |
| OP_MOD             |          -    |       9.59%  |        -   |          -    |
| OP_EQ              |          -    |       9.59%  |        -   |          -    |
| OP_CALL            |          -    |          -   |     6.65%  |          -    |
| OP_RETURN          |          -    |          -   |     6.65%  |          -    |
| OP_GET_LOCAL       |          -    |          -   |     6.65%  |          -    |
| OP_STR_INTERP_I    |          -    |          -   |        -   |        7.60%  |
| OP_STR             |          -    |       1.73%  |        -   |        7.68%  |

Universal (≥5% on 3 of 4 programs) - these convert first because
their wins apply across every workload:

1. OP_GET_GLOBAL
2. OP_SET_GLOBAL
3. OP_JUMP_IF_FALSE
4. OP_OVER
5. OP_LE
6. OP_LOOP
7. OP_INC
8. OP_ADD (integer fast path; string branch stays on parity bridge)

Benchmark-specific (≥5% on only 1–2 programs) - added later if the
universal set leaves the binary still short of the 1.5 KB headroom
target:

9. OP_INT_U8 (immediate constants; fizzbuzz-heavy)
10. OP_CALL / OP_RETURN / OP_GET_LOCAL (Phase 7 functions)
11. OP_STR_INTERP_I (Phase 7 c8a String())
12. OP_EQ / OP_MOD (fizzbuzz; small wins each)

### Differences from section "Phase 6b (deferred)" shortlist

The original shortlist (captured at Phase 6 close) and the refreshed
one disagree in two ways:

- **OP_OVER is universal-hot, not loop-specific.** The original doc
  listed it at 8% loop only; the refresh shows it at 6–8% across all
  four programs. Every `for i in 1...N` loop touches it on the
  comparison; this is a structural pattern, not a benchmark artefact.
  Promoted to the must-convert set.
- **OP_CALL / OP_RETURN / OP_GET_LOCAL are missing from the original
  list.** Phase 7 added function calls; the original benches couldn't
  see them. The new `calls_loop` bench shows each at 6.65%. They are
  benchmark-specific (no other program exercises them), but
  call-heavy programs are common - keep them in the second-tier
  shortlist.

OP_INT_U8 stays second-tier: 19% on fizzbuzz but ~0% on loop / string
programs. Convert only if needed after the universal set lands.

### Per-commit binary-delta targets

| Commit | Description | Target delta | Cumulative headroom |
|--------|-------------|--------------|---------------------|
| 1 | Bench refresh + this appendix | 0 B | 145 B |
| 2 | dispatch.s skeleton + OP_GET_GLOBAL | −50 to −300 B | 195–445 B |
| 3 | OP_SET_GLOBAL | −80 to −150 B | 275–595 B |
| 4 | OP_JUMP_IF_FALSE | −100 to −180 B | 375–775 B |
| 5 | OP_OVER + OP_LE (paired - loop comparison) | −150 to −250 B | 525–1025 B |
| 6 | OP_LOOP | −60 to −120 B | 585–1145 B |
| 7 | OP_INC | −60 to −100 B | 645–1245 B |
| 8 | OP_ADD (integer fast path) | −120 to −200 B | 765–1445 B |
| 9+ | (continue with second-tier if headroom < 1.5 KB) | varies | - |
| close | Phase 6b close: re-bench, lock budget gate, doc updates | 0 B | ≥1500 B target |

Targets are deliberately conservative; design doc 008 section Cost expected
−2.5 KB from this work, but the Phase 6 close revealed that
cc65-vs-source estimates run 3–4× over. The Phase 6b stop condition
is **headroom ≥ 1.5 KB**, not "convert all 8 opcodes" - if commits
2–5 already cross 1.5 KB, the phase closes there.

### Stop condition

Stop adding handlers when:
- **Binary headroom ≥ 1.5 KB** (funds the platform-APIs first sub-slice
  (now Phase 10) + margin), AND
- **`make ci` fully green** on every commit.

If headroom plateaus below 1.5 KB after 8+ conversions, stop and
combine with the Phase 2 REPL polish removal (~800 B–1.5 KB per the
ROADMAP "Maybe / probably never" item 26) before the platform-APIs
first slice (now Phase 10) lands. Do not chase headroom by converting hand-tuned asm
beyond the universal set - diminishing returns and the
cc65-ABI-boundary maintenance cost compounds.

### Commit 1 status

Bench programs and this appendix landed; no production code change.
Cumulative binary: 40,559 B (unchanged). Cumulative headroom: 145 B.

### Why paused (2026-05-24, same session as commit 1)

Two byte-arithmetic checks killed the original commit-2 plan and the
inline-asm fallback both, before any line of dispatch.s shipped:

**Full skeleton (jump table + parity bridge)** - design doc 008's
original Phase 6b approach. ~600 B all-in (512 B jump table +
~50 B fetch loop + ~30 B parity bridge). vm.c's switch can't be
deleted in the same commit because the parity bridge still JSRs into
it. Native OP_GET_GLOBAL handler (~80 B) replacing one table entry
doesn't shrink the C switch. **Net delta: +550 to +700 B** - breaks
the ProDOS ceiling. The original "−50 to −300 B" target was wrong.

**Inline-asm replacement** - the obvious fallback. Each conversion
replaces one case body in `vm_run`'s switch with a JSR to a ca65
function. No skeleton tax. Per opcode:
- Delete C case body: −40 to −60 B
- Add ca65 handler: +40 to +60 B (bounds + stride-3 multiply +
  3-byte struct copy + JSR value_retain + INC vm_sp)
- Add `#ifdef __CC65__` shim: +5 to +10 B

**Net delta per opcode: −10 to +10 B. Essentially break-even.**

The win from inline-asm is **cycles**, not bytes: eliminating cc65's
stride-multiply runtime call + software-stack push per dispatch.
Worth doing eventually for REPL latency and GR plot rate, but it
doesn't fund the platform-APIs byte budget (now Phase 10).

The design doc 008 section Cost "−1–2 KB binary" projection assumed full
skeleton + ~10 hot-handler conversions + eventual deletion of every
remaining case body once every opcode is native. That's a
multi-commit, multi-day, post-Phase-6b end state - not an interim
commit's worth of work.

### What replaces this for platform-APIs funding (now Phase 10)

Both "easy lever" funding paths from the project memory had already
shipped:
- **Phase 2 REPL polish removal** landed in commit `78f0209`
  (2026-05-23). Plain `>` prompt + implicit-print already in
  `src/repl/repl.c`. No fresh reclaim.
- **Feature audit** landed 2026-05-23 (LESSONS.md line 56) - 836 B
  saved via `:raw` + Tier-3 keywords + hex/bin/oct prefixes +
  underscore separators.

The platform-APIs phase will ship a **smaller first slice** (text-
positioning + memory access only - `home`, `htab`, `vtab`, `peek`,
`poke`) that fits within current headroom plus a small targeted
sweep. Display modes (`text`/`normal`/`inverse`/`flash`) and GR
move to a later Phase 10 slice once Phase 8 (hardware-capability
detection and the multi-binary build matrix) is in place - this is
exactly the restructure that promoted the build matrix into Phase 8
on 2026-05-24.

### Resuming Phase 6b

The Phase 6b plan still makes sense - as a **speed** optimisation
when REPL latency or GR plot rate becomes the bottleneck. The
bench infrastructure landed in commit 1 (`tests/bench/calls_loop.swift`,
`tests/bench/string_interp_loop.swift`) is the measurement baseline
for that future attempt. The conversion order above (universal-hot
first) is also still right.

Resume when at least one of:
1. Real-hardware measurement shows REPL latency exceeds the
   CONSTRAINTS.md 750 ms gate;
2. A Phase 10 GR demo runs visibly slowly;
3. A binary-cut path opens that overshoots ProDOS headroom enough
   to absorb the ~600 B skeleton tax (e.g., the multi-binary build
   matrix lets `SWIFTII.SYSTEM` shed extras-only code).
