# 016 - Family B bigger programs: low-RAM source window + disk streaming

As built in this design slice, source became disk-bounded through a 4,096 B
streaming window. Later Family B features spent some of that RAM again; the
current caps are authoritative in `CONSTRAINTS.md`, `MEMORY_MAP.md`, and the
Makefile. At the time this design landed the caps were: bytecode arena
**2,304 B**, const pool **1,024 B**, `MAX_FUNCS` 24 / `MAX_GLOBALS` 48,
Runner image 6,144 B, and heap 6,656 B. This extends
[015](015-bigger-programs-pascal-toolchain.md), which left true disk
streaming as a later optimization. Family A (the REPL disks) is
explicitly **out of scope and stays byte-identical**.

## Problem

Phase 15 shipped the Compiler→Runner split, but the headline goal -
"programs bounded by disk, not RAM" - was not reached. The 2026-06-07
decision loaded the whole source into RAM because the lexer/parser
reference source by `(pos,len)` and string interpolation re-lexes
source sub-ranges. Caps before this design slice:

| Buffer | Compiler | Runner |
|---|---|---|
| Source (`FILE_SRC_SIZE`) | 2,816 B | - |
| Bytecode arena (`FILE_BC_SIZE`) | 1,792 B | 1 (executes from image) |
| Heap (`HEAP_SIZE`) | 768 B (const pool) | 9,216 B (runtime) |
| `.swb` image (`SWB_IMAGE_SIZE`) | - | 4,096 B |

Both binaries were **RAM-full by design**: BSS was packed against $BB00
($BF00 − $0400 cc65 stack). Free RAM then was ~96 B (Compiler) and
~31 B (II+ Runner). The `make size` "headroom" column (8.6 KB / 17.4 KB)
is *file* headroom, not RAM - BSS isn't in the file. So bigger buffers
cannot simply be `-D`'d larger; the bytes have to come from somewhere.

The user goal (2026-06-11): **much larger Family B programs**, possibly
via disk streaming.

## Findings - why streaming is cheaper than 015 assumed

The 2026-06-07 "whole source in RAM" decision called a sliding window
"a large, risky refactor across every identifier/string path." A code
audit (2026-06-11) shows the deferred-reference surface is much
smaller:

1. **Symbol tables copy names; nothing long-lived points into source.**
   `globals`/`funcs`/`locals` store names NUL-padded to `IDENT_MAX`(=12)
   bytes - `IDENT_MAX-1` = 11 significant chars
   ([globals.h](../../../src/compiler/globals.h),
   [funcs.h](../../../src/compiler/funcs.h),
   [locals.h](../../../src/compiler/locals.h)). Lookup is by bytes, not
   by source offset. (Since 2026-06-17 a *declaration* longer than 11
   chars is a compile-time error - `expect_decl_name` in statements.c,
   message "name longer than 11 chars" - rather than a silent truncation
   that could collide on a shared prefix; the store is therefore always
   the verbatim source name.)
2. **String interpolation is token-local.**
   [strings.c](../../../src/compiler/strings.c) walks and sub-lexes only
   the *current* `TOK_STR`'s `tok_pos..tok_pos+tok_len` range, while it
   is still the current token (`compile_interp_expr` re-inits the lexer
   inside that span and restores it). A window that holds the whole
   current literal suffices - and literals are capped far lower by the
   constant heap anyway.
3. **All other deferred `(pos,len)` uses are within-statement.** Twelve
   `name_pos = p->L.tok_pos` sites (statements.c ×7, pratt.c ×2,
   types.c ×1, plus two `span_equals` checks on the current token) save
   an identifier position and consume it a few tokens later in the same
   declaration/call. None survive across a statement separator.
4. **A safe refill point already exists.** `parser_skip_separators`
   (statements.c) runs between statements at every nesting level. No
   deferred reference crosses it, so the window may slide there and
   only there.
5. **~4–5 KB of low RAM is free in Family B.** $0C00–$1BFF is the
   established launcher→tool handoff region (`STAGED_SRC_ADDR` $0C00,
   `STAGED_LEN_ADDR` $1B06, `EDIT_PATH_ADDR` = $0C00). On a Family B
   disk only the length-prefixed path handoff uses it, and it is dead
   the moment the Compiler copies the path out at startup (the editor
   already follows this copy-out-then-reuse protocol). The Family A
   XLC loaders' use of $0800–$17FF as chain scratch does not apply to
   the MAIN-only Family B chain. The MLI I/O buffer at $1C00–$1FFF and
   text page 1 ($0400–$07FF) are untouched. $0800–$0BFF is 40-col
   **text page 2** - never displayed by SwiftII, but reserved by the
   staging convention; see Open decisions.

## Design

Two tiers, the second building on the first. A third (streaming the
*bytecode* out) is consciously deferred - see "What we are not doing."

### Tier 1 - re-home the source window + rebalance BSS

Move the Compiler's source buffer from BSS (`static char
s_src[FILE_SRC_SIZE]`, [compiler_main.c](../../../src/main/compiler_main.c))
to an absolute low-RAM window at $0C00–$1BFF (4,096 B; $0800 start =
5,120 B if the text-page-2 audit clears). `s_src` becomes a pointer
constant; the path at `EDIT_PATH_ADDR` is copied to a small BSS buffer
before the first source read (already effectively the case - the path
is consumed at startup).

The 2,816 B of BSS this frees is rebalanced into the real growth
levers, e.g.:

| Define | Now | Tier 1 (proposed) |
|---|---|---|
| `FILE_SRC_SIZE` (Compiler) | 2,816 | 4,096 (low-RAM window) |
| `FILE_BC_SIZE` (Compiler) | 1,792 | ~3,584 |
| `HEAP_SIZE` (Compiler const pool) | 768 | ~1,280 |
| `MAX_FUNCS` (both binaries, must agree) | 16 | 24 |
| `MAX_GLOBALS` | 32 | 48 |
| `SWB_IMAGE_SIZE` (Runner) | 4,096 | ~6,144 |
| `HEAP_SIZE` (Runner) | 9,216 | ~7,168 |

Exact splits are set by the linker map after the change (BSS must stay
under $BB00 with margin); the table is the intent, not a contract.
The Runner pays for its bigger image out of its runtime heap - still
~3.5× the Family A heap. The //e Runner has ~2.1 KB more slack than
the II+ one; sizes are set by the II+ build (one shared `.swb` cap).

Net effect, no streaming yet: **source ~4 KB (2× REPL), bytecode 2×,
funcs/globals headroom for bigger programs.** Low risk; no
lexer/parser logic changes.

#### Tier 1 as-built (2026-06-11)

Shipped exactly per the table, except the Runner heap settled at
**6,656** (not 7,168) - the `MAX_FUNCS`/`MAX_GLOBALS` table growth in
the Runner (~450 B across funcs.c entries + the VM's `s_globals`) comes
out of the same pool as the image bump. Final defines
(Makefile `COMPILER_DEFS`/`RUNNER_DEFS`): Compiler
`FILE_SRC_SIZE=4096 FILE_BC_SIZE=3584 HEAP_SIZE=1280`; Runner
`SWB_IMAGE_SIZE=6144 HEAP_SIZE=6656`; both `MAX_GLOBALS=48 MAX_FUNCS=24`
(config.h gained `#ifndef` guards). On target `s_src` is
`#define`'d to `(char *)0x0C00` (compiler_main.c, with a preprocessor
guard that `FILE_SRC_SIZE` cannot overrun the $1C00 MLI buffer); the
host build keeps the BSS array. Window start **$0C00 chosen** (text
page 2 left alone - open decision 1 resolved conservatively).

Measured: file sizes unchanged (COMPILER 32,082 / RUNNER 23,281 //
//e 21,178 - the window isn't in the file image); BSS now ends at
$BA37 (Compiler, 201 B short of $BB00), $B9D8 (II+ Runner, 296 B),
$B197 (//e Runner, 2,409 B). 193 host tests green; Family A
interpreters AND both launchers byte-identical (sha256, rebuilt from
HEAD and diffed). Emulator verification rides the already-owed Family B
pass (a >2.8 KB sample now compiles where it previously errored
"source too big for memory").

### Tier 2 - statement-boundary streaming (source disk-bounded)

Turn the low-RAM window into a sliding window over the source file:

- **Mechanism.** The source file stays open during the compile
  (`pf_open_read` → repeated `pf_read`). At each
  `parser_skip_separators`, if the file is not exhausted and
  `L.pos` has passed a high-water mark (e.g. half the window):
  `memmove` the unconsumed tail `[L.pos..L.len)` to the window start,
  top up from disk, and rebase `L.pos`/`L.len`/`tok_pos` by the slide
  amount. Lexer offsets stay window-relative `uint16_t`; the `line`
  counter is incremental and unaffected; EOF falls out naturally when
  a refill adds nothing and the lexer reaches `L.len`.
- **Gating.** The refill is a `WITH_SWB`-gated hook on `Parser`
  (function-pointer field, NULL = no streaming), so: Family A builds
  (WITH_SWB off) are **byte-identical**; the lexer itself is
  untouched; the host build (WITH_SWB on) can drive the hook with an
  artificially tiny window to unit-test slides.
- **Belt-and-braces.** Copy the 12 within-statement `name_pos` sites
  into 12-byte locals at the point of capture (IDENT_MAX bounds them),
  removing any dependence on "no slide mid-statement" being true
  forever. Estimated ~100–200 B; optional if the slide-point invariant
  is asserted instead.
- **New rule (documented, compile-error enforced):** a single statement - including any string literal - must fit half the window (~2 KB).
  In practice literals are bounded by the constant pool first.
- **MLI buffers.** One file open at a time still holds: source is
  closed before the `.swb` is written (bytecode stays in the RAM arena
  until compile end). The single $1C00 buffer suffices unchanged.

Net effect: **source size is disk-bounded.** The binding caps become
the bytecode arena (~3.5 KB) + constant pool + funcs/globals tables -
empirically ~2:1 source:bytecode, so roughly **6–10 KB programs**
compile and run, vs 2.8 KB today.

#### Tier 2 as-built (2026-06-11)

Shipped as designed - [srcwin.c](../../../src/compiler/srcwin.c) sliding
window (slide keeps the current separator token resident, `delta =
tok_pos`, top-up via `pf_read`; triggers once `pos` passes cap/2),
`WITH_SWB`-gated `refill`/`refill_ctx` hook on `Parser` installed via
`compiler_set_refill()`, hook called from `parser_skip_separators` -
with two findings the design audit missed, both caught by the new host
tests:

1. **Else-probe snapshots break across slides.** `parse_if` and
   `parse_if_let` save the whole `Lexer`, call
   `parser_skip_separators` to look for `else`, and *restore the
   snapshot* if none follows - restoring pre-slide offsets over
   post-slide window content (symptom: the parser silently re-read slid
   bytes; "undeclared name" deep into a streamed file). Fix:
   `skip_separators_noslide` (statements.c) masks the hook during the
   probe; on restore the outer loop re-skips the same separators with
   sliding re-enabled. Family A aliases it to the plain skip via macro
   (no codegen change). These two probes are the **only** sites that
   restore a Lexer across a separator (the other three `Lexer saved`
   sites span single `lexer_next` calls - slide-free); the audit's "12
   within-statement tok_pos uses" claim stands, but **any future
   save-skip-restore lookahead must use the noslide variant.**
2. **A too-long statement can truncate silently, not just error.** The
   lexer EOFs at the window edge mid-statement, and the partial
   statement can still parse cleanly (`a = 1 + 1 + …` cut at any `+`
   is valid). compiler_main therefore rejects on `!s_win.eof` **even
   when the compile succeeds** ("one statement is too long for
   memory"). The eof flag, not the error code, is the contract.

Budget: Tier 2 costs **+801 B** of Compiler code/rodata (32,082 →
32,883), paid by the bytecode arena: `FILE_BC_SIZE` settled at **2,816**
(not Tier 1's 3,584; later re-shaved to **2,304** with the const pool at
**1,024** by the section Follow-ups code - those are the final caps), BSS ends
$BA6B (148 B under $BB00). Runner untouched from Tier 1. Effective program
cap is now the bytecode arena + const pool - at the observed ~2:1
source:bytecode ratio, roughly **5–6 KB of source**, with `.swb` ≤
2,816 + 1,280 + funcs ≈ 4.2 KB, comfortably inside the Runner's 6,144 B
image. Tests: 3 new host tests (196 total) - stream-vs-whole output
diff across windows 192–1024 B over a multi-KB generated source
(interpolation + calls + if/else across slide points), small-file eof,
and the silent-truncation case. Family A interpreters + both launchers
byte-identical (sha256 vs HEAD rebuild). Emulator verification rides
the owed Family B pass.

#### Follow-ups (2026-06-11, user-requested)

- **Compile/run progress.** The Compiler shows an in-place percent line
  under "compiling <path>": on each completed window slide,
  `refill_progress` (compiler_main) computes bytes-consumed (`SrcWin.rd`
  minus what's still ahead of the lexer) over the file size (new
  `pf_size` = MLI GET_EOF / host ftell) and rewrites " NN%" at column 1
  via `platform_htab` (gate widened to `WITH_SWB`); "100%" prints on
  completion. Sub-window files never slide and show no percent (they
  compile near-instantly). Deliberately 16-bit math -
  `consumed/(total/100)`, ≤1% error - because the natural 32-bit form
  dragged ~700 B of cc65 long-div runtime into a binary with no room.
  (Dots-per-slide shipped first the same day and were replaced by
  percent on user feedback.) The line starts at " 0%" as soon as a
  streaming compile begins. The Runner prints "loading <path>" before
  the multi-KB `.swb` read, covering the otherwise-silent chain gap.
- **Ctrl-C breaks a running program (Runner only).** The VM polls the
  keyboard in the OP_LOOP handler - every infinite loop passes through
  the backward jump (recursion is depth-capped), so straight-line code
  pays nothing; a /64 tick keeps the $C000 read off the tightest loops.
  Gated `WITH_SWB && __CC65__`: the Runner is the only cc65 binary with
  both, so Family A interpreters stay byte-identical (they still have
  no break - a Family A `while true` hangs until reboot, as before).
  On break the Runner prints `*break*` (new `SE_BREAK`), waits for a
  key, and chains back to the launcher. Limitation: while a program is
  *blocked in readLine*, Ctrl-C is just input - this interrupts running
  code, not waits.
- **"write error" was the 140 KB disk filling up.** Adding the 9.1 KB
  xbig source left the Family B disks 2,048/3,072 B free - too little
  for its ~3.5 KB `.swb` (7 blocks), so MLI WRITE returned $48 and the
  terse message hid it. Three fixes: (1) the four regression
  diagnostics (branch/mod/strint/nested, 6 blocks of dev-facing weight
  on every program disk) moved `datadisk/samples/` → `datadisk/tests/`
  (data-disk `TESTS/` only) - Family B free space is now 5,120 (II+) /
  6,144 (//e) B; (2) `pf_open_write`/`pf_write` set `pf_errno`, and the
  Compiler's create/write failures print `err=$NN` plus "disk full" for
  $48; (3) compile-progress cost the buffers again: `FILE_BC_SIZE`
  2,816 → **2,304**, const pool 1,280 → **1,024** (xbig: 80% / 74% of
  the new caps). Two-drive users get effectively unlimited `.swb` room
  by compiling sources on the data disk (100 KB free) - the `.swb`
  lands next to the source.
- **Editor capacity: II+ 3 KB → 4 KB.** The GAPBUF region ($0800–$1BFF,
  5,120 B) holds the gap buffer *plus* the dirty-diff `EditorScreen`
  frame; the frame was allocated 80 columns wide on both builds even
  though the II+ editor can never leave 40 columns (Ctrl-W is
  `LITE_IIE`-gated). `ED_COLS_MAX` is now 40 on the II+ launcher build
  (host keeps 80 for the tests), shrinking the frame 1,923 → 963 B and
  raising `GAPBUF_CAP` to 4,096 - the same size as the Compiler's
  source window. The //e build keeps the 80-col frame and the 3 KB
  buffer. Bonus: the II+ launcher's second (render) frame in high BSS
  shrank too, freeing ~960 B of launcher BSS. **Honest ceiling:** the
  editor cannot *match* the disk-bounded Compiler without paged editing
  (Maybe item 11) - files beyond the gap buffer (xbig.swift) remain
  file-selector-[X]-only.

### What we are NOT doing (and why)

- **Tier 3 - write-behind `.swb` bytecode** (stream bytecode to disk,
  backpatch jumps via MLI `SET_MARK`): **moved to ROADMAP "Maybe /
  probably never" item 27 (2026-06-11, user decision)** - reopen only
  if a real program hits the bytecode arena. It needs a second MLI I/O
  buffer (source + `.swb` open simultaneously), `SET_MARK`/seek support
  in prodos.c, a backpatch journal, **and a bcbuf redesign**
  (`bcbuf_rotate_func_into_arena` moves function bodies after
  compilation - incompatible with bytes already flushed to disk) - the
  most code for the least gain, because:
- **The Runner is the final wall regardless.** The Runner executes the
  `.swb` in place from `s_image[]`
  ([runner_main.c](../../../src/main/runner_main.c)); paging bytecode
  from disk mid-execution is a non-starter at 1 MHz. Its ~13 KB of
  discretionary RAM (image + heap) caps a realistic `.swb` at ~8 KB.
  Tier 3 would only move the wall from the Compiler's arena to the
  Runner's image - a ~2× gain, not unbounded.
- **Family A changes.** None. Lite/SAT/AUX interpreters and the REPL
  staged-run path must remain byte-identical (CI sha256 check).

## Decisions (settled at implementation)

1. **Window start: $0C00 (4 KB, safe) or $0800 (5 KB)?** $0800–$0BFF
   is 40-col text page 2 - SwiftII never displays page 2 (launcher,
   editor, and //e 80-col all render on page 1 main/aux), so it is
   *believed* safe, but the staging convention deliberately avoided
   it. Recommend: start at $0C00; widen to $0800 only with a page-2
   audit (PAGE2 soft-switch users, ProDOS quit-code behavior). Under
   Tier 2 the window size only affects refill frequency, not program
   capacity, so this matters mainly for Tier 1.
2. **Tier 1 alone first, or Tier 1+2 in one slice?** Tier 1 is
   shippable on its own and de-risks the buffer moves; Tier 2 is where
   "much larger" actually lands. Recommend two commits, one slice.
3. **Rebalance split** - bigger bytecode arena vs bigger Runner heap is
   a taste call once the maps are in (table above is the proposal).
4. **`MAX_FUNCS`/`MAX_GLOBALS` bumps** ride along (both binaries must
   agree on `MAX_FUNCS` for the `.swb` funcs section; the 12-B header's
   `funcs_count` already carries the count, so the format is unchanged - no version bump).

## Verification plan

- Host: existing 190+ tests green; new swb/streaming tests - compile a
  >5 KB sample through the streaming hook with a deliberately tiny
  (e.g. 256 B) window and diff output against a whole-buffer compile;
  slide-rebase unit test; statement-too-long error test.
- Host: the Phase 15 "diff file-program output vs the combined
  interpreter" suite naturally extends to the big sample.
- Target: `make size` budget rows still green (BSS < $BB00 with
  margin); Family A binaries byte-identical (sha256).
- Emulator (`make run-iz-compiler[-iie]`): compile + run a sample
  larger than 4 KB end-to-end on both Family B disks; re-run its
  `.swb` via the file-selector X path.
