# 020 - Tier-2 Saturn paged Runner: the slot-clobber bug + compiler-tier tradeoffs

This documents (1) a hard-won bug in the Tier-2 Saturn paged bytecode driver
that silently corrupted large programs, and (2) the pros and cons of the three
on-target Family B compiler/Runner builds, which is what determines *which
programs run on which disk*. The two are linked: the bug only shows up on
programs big enough to exercise the Saturn tier's paging, and understanding the
tiers is what made the bug findable.

## The Family B compiler/Runner builds

A Family B program is compiled to a `.swb` by a COMPILER, then executed by a
RUNNER. There are three on-target *tiers* of each, differing in **where bytecode
lives** and therefore in three independent RAM budgets. The flat Tier 1 ships in
two flavors — II+ and a //e-native build (`WITH_IIE` case rendering + a
firmware-80-col Runner, disk `swiftii-iie-compiler.po`) — which share the flat
compile-side budget below and the same flat program model; they differ in render
path and runtime heap (the //e build carries no Videx static BSS, so its Runner
heap is the higher 2560 B), so they are one column here:

| | flat (Tier 1, II+/​//e) | Saturn (Tier 2) | //e aux (Tier 3) |
|---|---|---|---|
| disk | `swiftii-iip-compiler.po` / `swiftii-iie-compiler.po` | `swiftii-iip-sat-compiler.po` | `swiftii-iie-aux-compiler.po` |
| bytecode store | none — whole image in MAIN | Saturn 128K banks ($D000 window) | //e aux 64K ($2000) via AUXMOVE |
| compile bytecode window (`FILE_BC_SIZE`) | **1834 B** (whole program) | **640 B** (top-level scratch + 1 in-progress fn) | **896 B** (same) |
| compile const-pool heap (`HEAP_SIZE`) | **768 B** | **704 B** | **744 B** |
| total compiled code ceiling | ~1834 B | ~36 KB (`AUX_BC_MAX`) | ~36 KB |
| runtime heap (Runner `HEAP_SIZE`) | **II+ 2136 / //e 2560 B** | **1792 B** | **2560 B** |
| runtime bytecode window (`BC_WINDOW`) | n/a (flat) | 512 B | 512 B |
| machine | any II+ (or any //e, //e build) | II+ + Saturn 128K | //e + 64K aux |

The paged builds (`WITH_AUX_COMPILE` / `WITH_AUX_BC`) **flush completed,
immutable function bodies out to the bank** as they go, so total code can reach
~36 KB. The price is paid in main RAM (the resident scratch window, the runtime
bytecode window + tail buffer, the bank driver), which is why every paged budget
is smaller than you might expect.

### Telling the disks apart

All three `COMPILER.SYSTEM`/`RUNNER.SYSTEM` builds use the **same filenames**, so
neither the launcher nor the user can read the tier off the directory. Two
disk-facing cues carry it, both chosen by the disk at build time (never a runtime
hardware probe — the same disk-not-hardware rule as `LITE_IIE`; a Saturn probe
would mislabel a *flat* disk booted on a Saturn-equipped machine):

- **Launcher banner.** A disk that shares a launcher source with a sibling under
  the same `COMPILER.SYSTEM` filename ships its own launcher build that tags the
  banner: the II+ Saturn disk (`-DFAMILYB_SATURN`, `build/boot_launcher/sat/`)
  reads `SwiftII Compiler ][+ Saturn`, and the //e *aux* disk (`-DFAMILYB_AUX`,
  `build/boot_launcher/iie-aux/`) reads `SwiftII Compiler //e aux`. The flat II+
  disk's reads `SwiftII Compiler ][+`, the //e flat disk's `SwiftII Compiler //e`.
  (A few bytes over the base launcher — the banner string only.)
- **README Runner line.** Each disk's `README.TXT` names the Runner's required
  machine/card for its tier (II+ flat = no extra card, //e flat = any //e,
  Saturn = Saturn 128K, //e aux = 64K aux), substituted per disk from
  `README_RUNNER` at disk-build time.

### Pros and cons

**Flat II+ Tier 1.** *Pro:* the whole program's bytecode is resident, so
**top-level-heavy** code (long top-level statement runs, few functions) is fine
up to 1834 B; no paging, no paging bugs; runs on a bare II+ with no card. *Con:*
**total** code is capped at the 1834 B buffer — a function-heavy program whose
bytecode exceeds 1834 B simply will not compile, even though each function is
small.

**Saturn Tier 2.** *Pro:* function bodies flush to the Saturn 128K banks, so
**function-heavy** programs up to ~36 KB compile; runs on a II+ (no //e needed).
*Con:* it has the **smallest** of everything that stays in main RAM — the 640 B
resident scratch (so a >640 B top-level block overflows: "program too big for
memory"), the 704 B const-pool heap (so a string-heavy program overflows: "heap
full"), and the 1792 B runtime heap (it pays ~1.5 KB of main RAM for the 512 B
window + 1024 B `SWB_TAIL_CAP` const/funcs tail + the bank driver). Plus it is
the **least-exercised path** — see the bug below.

**//e aux Tier 3.** *Pro:* same ~36 KB code ceiling as Saturn but with more
headroom — 896 B resident scratch, 744 B compile heap, and the **largest**
runtime heap (2560 B) because the bytecode pages to the *separate* aux 64 KB, so
its window/tail buffers don't compete with the main-RAM heap, and it carries no
Videx 80-col code. *Con:* needs a //e with a 64K aux card; the resident scratch
(896 B) is still well below the flat buffer (1834 B), so a very top-level-heavy
program still overflows.

### How this lands on the oversize demos

The data disk ships three oversize showcases. Two run on **all three tiers**
(`xbig`, `xgrdemo`); one is the deliberate **paged-only** showcase (`xfuncs`).
Emulator-verified on izapple2 (each prints its checksum on flat II+, Saturn, and
//e aux); the full matrix lives in `datadisk/README.md`.

- **`xgrdemo`** (graphics showcase, ~1 KB bytecode, ~188 B const pool): wrapped
  so each scene is a function → top-level scratch stays tiny → all three tiers.
- **`xbig`** (the deliberately-large eight-section number tour, ~1.8 KB
  bytecode): originally **Tier-1-only** — top-level-heavy (overflowed the paged
  640/896 B compile window) *and* string-heavy (its ~800 B const pool overflowed
  the Saturn/aux 704/744 B compile heap, and that const pool also floors the
  runtime heap, pushing the peak to ~2124 B > Saturn's 1792 B). Two changes make
  it all-tier without changing what it computes (checksum stays `6265`):
  1. **Each section is wrapped in a function.** Section bodies flush to the
     paged store (top-level scratch → tiny, clears the 640/896 window).
  2. **The print labels were shortened** (terser banners only — no computation
     touched). Const pool **784 → 644 B** (under Saturn's 704 compile heap), and
     since the const pool floors the runtime heap, that plus the per-section
     locals freeing on return drops the runtime peak **~2124 → ~1719 B** — under
     the Saturn Runner's 1792 B heap with ~73 B margin (more than the ~12 B the
     old top-level xbig left on the II+ Runner). `STRING_POOL_SLOTS` is a fixed
     constant (16), so the host-replica peak is exactly the on-target number.

  Note the runtime peak is set by **bump-allocator fragmentation**, not the live
  set: the heap reclaims only the *topmost* freed block (LIFO), so a string-
  building loop (`row = row + String(n) + " "`) buries each previous version
  mid-heap where it can't be reclaimed until `heap_reset`. Function wrapping
  helps the *window/const* budgets but not this — the fragmentation persists
  across the run. The lever that actually moves the runtime peak is **less const
  pool / less string building**, which is why shortening labels was the fix.

  **Heritage note.** The II+ Runner heap was bumped to **2136 B**
  (`RUNNER_IIP_DEFS`) for the *old* top-level xbig (~2124 B peak); the current
  xbig peaks ~1719 B so that margin is now generous, but the heap is left as-is
  (other programs may use it, and it still links with the Videx 80-col code).
  Lesson stands: the largest shipped program is a runtime-heap canary the small
  FBTESTS don't catch.
- **`xfuncs`** (function-heavy, 20 functions, ~1.99 KB bytecode): its **total**
  bytecode exceeds the flat 1834 B whole-program buffer, so the flat II+ compiler
  rejects it ("compiled bytecode exceeds buffer") while the paged tiers — capped
  only by the ~36 KB paged ceiling, not the resident window — compile it fine.
  Kept paged-only **by design** as the one demo that proves the paged compilers'
  larger ceiling. (See the host-replica recipe below to check this without an
  emulator.)

### Host replicas for tier budgets (no emulator needed)

`build/host/swbc` (flat, `FILE_BC_SIZE=16384`) compiles anything. To check a
program against a *specific tier's* compile budgets, build a replica from the
swbc source set with that tier's flags, e.g. Saturn:

```
cc … -DWITH_AUX_COMPILE -DFILE_BC_SIZE=640 -DHEAP_SIZE=704 -DMAX_FUNCS=24 \
     -DMAX_GLOBALS=48 -DWITH_SWB -DWITH_BIGLANG -DWITH_RANDOM -DNO_ARRAY_RUNTIME …
```

and `//e aux` (896/744), flat (1834/768). These reproduce "program too big for
memory" (window) and "heap full" (const pool) exactly. The *runtime* heap is
separate: build a runner with the tier's `HEAP_SIZE` to find a program's peak.

**These host replicas do NOT model the Saturn-bank assembly** — only the paging
*logic*. The bug below lives in `saturn_bc.s`, which only runs on the emulator.

## The bug: a clobbered slot byte corrupts large Saturn programs

### Symptom

A large program (`xbig`, ~1.8 KB bytecode) compiled cleanly on the Saturn disk
(its `.swb` byte-identical to a flat compile) and ran fine on the **flat II+**
and **//e aux** Runners, but threw a generic "runtime error" on the **Saturn**
Runner. A small program (`xgrdemo`, ~1 KB) ran fine on Saturn. The error was not
heap (a host runner at the Saturn's 1792 B heap ran `xbig` fine), and the `.swb`
was identical regardless of compiler — so the corruption was in the Saturn-bank
*execution*, not the bytecode.

### Root cause

The boot launcher detects the Saturn and parks its **slot number** at
`SX_SAT_SLOT_ADDR` ($1B04) in the $1B00 launcher→interpreter handoff page. The
COMPILER reads it at startup and patches `saturn_bc.s`'s bank-select switches
from it; so does the chained RUNNER. The base address of the Saturn's `$C0xx`
soft switches is computed as `(slot + 8) * 16`.

The $1B00 page was *assumed* to survive the COMPILER→RUNNER chain (`config.h`
says so), but **it does not**: the compiler's source-read scratch overlaps it,
so compiling leaves a stray **source byte** at $1B04. The Runner then reads that
byte as the slot:

- `xgrdemo`'s leftover byte happened to be `$20` (a space) → base `($20+8)*16` =
  `$80` → the **built-in language card** ($C080…), which is real RAM, so the
  bytecode store round-tripped **by accident**.
- `xbig`'s leftover byte was `$3D` (`=`) → base `($3D+8)*16` low byte `$50` →
  **video soft switches** ($C050 graphics / $C055 page-2) → store reads/writes
  hit video registers, not RAM → total bytecode corruption → the VM executes
  garbage → "runtime error".

(On the izapple2 `-s0 saturn` config the real slot is 0; `xgrdemo` worked only
because `$20` and `0` map to the same `$80` base. Any program whose stray byte
maps elsewhere fails — it is silent, data-dependent corruption.) The Saturn tier
had only ever been exercised by small samples whose bytecode fits the resident
1024 B tail and barely pages the bank, so this stayed latent (the Tier-2 Saturn
driver was flagged as the less-proven path).

### Fix

`src/main/compiler_main.c` (guarded by `BC_STORE_SATURN && __CC65__`): stash the
slot read at startup and **re-deposit it at $1B04 immediately before chaining
the Runner** (after `stage_swb_for_runner`, before `chain_exec`). The chain READ
loads RUNNER.SYSTEM to $2000+, leaving $1B00 intact, so the Runner reads the
correct slot. The Runner is unchanged; the SWIFTSAT extras REPL is unaffected
(it chains directly from the launcher, never via the compiler).

### How it was found (debug methodology)

1. **Localize to the store, not the VM:** a `SAT_VERIFY` build of the Runner
   (`load_paged`) stages each bytecode chunk, reads it straight back, and
   checksums — proving the Saturn store round-trip corrupts `xbig` but not
   `xgrdemo` (costs ~430 B BSS, so trim `HEAP_SIZE` for the diagnostic build).
2. **Remove content-dependence:** a known ramp pattern through the store at the
   same offsets corrupts identically — so it is not the bytecode values.
3. **Read the slot:** printing the byte at $1B04 showed ASCII (`=`/space), not a
   slot number — and the two programs differed, pinning it to a source-byte
   clobber.
4. **Confirm:** hardcoding `saturn_bc_init(0)` makes the round-trip clean.

The host cannot reproduce any of this (its `aux_store` is a linear `memcpy`
stub). Lesson: **a faithful host replica of the paging *logic* is not a replica
of the bank *hardware* path** — for Saturn-bank bugs you must instrument the
on-target Runner and read the emulator's screen.

## Migration

No on-disk format change. The fix is one re-deposit in the Saturn compiler; flat
and //e builds compile it out and stay byte-identical. Existing `.swb` files and
the FBTESTS suite are unaffected (verified: the Saturn `compiler-sat` acceptance
sweep passes, 0 fail).

## A compiler bug found + fixed: a `let` in a conditionally-taken branch leaked the stack

Surfaced while making `xbig` all-tier (wrapping its bubble sort into a function),
and **now fixed** (`parse_if` in `compiler/statements.c`).

**Symptom.** A bubble sort —

```
while swapped {
  swapped = false
  for i in 0..<n - 1 {
    if a[i] > a[i + 1] { let t = a[i]; a[i] = a[i + 1]; a[i + 1] = t; swapped = true }
  }
}
```

— valid SwiftII that runs **correctly at top level**, aborted with **`SE_TYPE_MISMATCH`**
(`errors.h` code 5, surfaced as the generic "VM halted with error") when the same
code sat **inside a function body**: an opcode received a wrong-typed value because
a stack/local slot had been clobbered.

**Root cause (a compiler codegen bug, not the VM).** A range `for` keeps its loop
bound on the value stack (the `OP_OVER` peek), so the loop body must be
stack-neutral — `parse_while`/`parse_for_in` enforce this by calling
`parse_block_scoped`, which pops every body-declared local before the back-jump.
But **`parse_if` called the unscoped `parse_block`**, so a `let`/`var` declared in
an `if`/`else` branch was *not* popped on that branch's path. When the branch was
**taken on some iterations and skipped on others**, the local was pushed only on
the taken ones, while the enclosing loop's scope-cleanup popped it
**unconditionally** — a one-slot drift per skipped iteration that eventually fed a
wrong-typed value into an op. (Hence top level was fine: there body locals are
*globals*, never on the stack; and a *single*-pass loop where the branch is always
taken was fine — the drift needs a skipped iteration. It is **not** about arrays:
a non-array condition like `if i == 0` reproduces it identically.)

**Fix.** Both `if`/`else` arms now scope their locals, via a shared
`parse_block_scoped_auto(p)` helper (= `parse_block_scoped(p, locals_count())`,
gated on `SWIFTII_EXT_COMPILER`). The *same* helper replaced the inline
`parse_block_scoped(p, locals_count())` at every other
conditionally-/repeatedly-executed body too (while, for-in range + array, the
if-let some-arm) — folding ~5 inline copies into one callee, which made the fix a
**net code reduction**, so it fits even the at-the-MAIN-ceiling extras REPLs
(SWIFTSAT / SWIFTAUX) with no budget trade. (A first cut that inlined the scope
at the two `if` arms overflowed those two binaries' MAIN load image by 1 byte —
in their cfg the LC segment loads into MAIN, so LC growth counts against MAIN;
the consolidation is what clears it.) Verified: the failing cases pass, the
bubble sort runs in a function, host suite 231/0 + the on-disk suites, all four
REPL + the Family B compiler/Runner binaries link, and `xbig` (bubble sort restored) prints
`checksum = 6265` on real izapple2 flat II+, Saturn, and //e aux. It was
deterministic and platform-independent all along (pure shared C — repros on the
host build), so real iron and Mariani hit it identically; the fix lands
everywhere the same way.

## Open questions

- Should the FBTESTS suite gain a deliberately-large (front-paging) Family B
  test so the Saturn bank path is regression-guarded by CI, not only by the
  oversize showcases? The current FBTESTS are all small enough to live in the
  resident tail and never page the bank.
