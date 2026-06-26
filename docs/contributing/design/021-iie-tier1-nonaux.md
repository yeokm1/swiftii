# 021 — A //e-native flat Tier-1 Family B toolchain (non-aux)

## What

Split the //e Family B compiler disk into two builds:

- **`swiftii-iie-compiler.po`** — a new **//e-native flat Tier-1** toolchain:
  the flat Compiler/Runner (whole `.swb` in MAIN, no aux paging) built
  `WITH_IIE`, with the Runner carrying the //e **firmware** 80-col arm
  (`IIE_80COL_DEF`). Runs on any //e, including one with only the 1 KB
  80-Column Text Card — no 64 KB extended aux card required.
- **`swiftii-iie-aux-compiler.po`** — the existing aux-paged Tier-3 build
  (`WITH_AUX_COMPILE` / `WITH_AUX_BC`), unchanged except in name/packaging;
  still needs the 64 KB extended aux card.

The distribution grows from eight disks to nine. Makefile binary vars: the
existing `A2IIECOMP`/`A2IIERUN` (and `COMPILER_IIE_*`) are renamed to
`A2IIEAUXCOMP`/`A2IIEAUXRUN` (`COMPILER_IIEAUX_*`), and the plain names now hold
the new flat //e binaries (`COMPILER_IIE_DEFS` / `RUNNER_IIE_DEFS`). Build dirs:
aux → `build/apple2/{compiler,runner}/iie-aux/`, flat //e → `.../iie/`.

## Why

A //e *without* the 64 KB extended aux card had no //e-flavored Family B disk —
it fell back to the **II+ Tier-1** toolchain, losing both //e benefits: the
firmware 80-col mode (a program's `text80()`) and native lowercase rendering
(`WITH_IIE`), getting instead the inverse-video case hack and, at best,
Videx-only 80-col. The 80-col fallback was the user-visible gap. A //e-native
flat build closes it without requiring the extended aux card.

## Alternatives considered

- **Auto-detect aux at runtime and pick the path.** Rejected: the project's
  settled rule is that the *disk* declares the machine — there is no runtime
  hardware probe (`$FBB3` is untrustworthy; emulators lie). Per-machine MAIN
  behavior is a build flag chosen by the disk, never a runtime ID probe.
- **Ship firmware 80-col on the existing aux build only.** Doesn't help the
  non-extended //e, which can't run the aux build at all.
- **Add firmware 80-col to the Compiler too.** Rejected: the Compiler is a
  transient 40-col tool, and the firmware arm overflows the at-budget Compiler
  (~194 B) — same reasoning as the existing aux //e Compiler, which is also
  40-col `WITH_IIE`.

## Cost

Two new binaries, both well within budget (no heap trim needed):

| Binary | Bytes | Headroom (of 40,704) |
|---|---|---|
| `COMPILER.SYSTEM` //e flat | 34,874 | 5,830 |
| `RUNNER.SYSTEM` //e flat | 29,528 | 11,176 |

The flat //e Compiler is *smaller* than the II+ one because `WITH_IIE` drops
`emit_inverse_letter` + the digraph branches (and we filter `WITH_INVERSE_JM`
out of `COMPILER_DEFS`, unneeded on the native render path). The flat //e Runner
fit the firmware 80-col arm at the full `RUNNER_DEFS` heap (2,560 B) because it
carries neither the II+ Runner's Videx static BSS nor the aux window — so the
~315 B firmware arm lands with room to spare.

No C/asm source changes — this is purely a build-matrix + packaging change.
Program-size caps are identical to the II+ Tier-1 (flat 1,834 B arena, 2,944 B
whole-`.swb` MAIN buffer); a //e that *does* have the extended aux card uses the
aux disk for bigger programs.

## Verification

`make apple2-familyb` (clean — remove `build/apple2/{compiler,runner}` first,
since Make keys off mtime not `-D` flags), `make size` (both new rows in
budget), `make disk-iie-compiler disk-iie-aux-compiler` (both images build with
ample free space), `make test` / `make sim` green. Emulator gate (human):
`run-iz-compiler-iie` on a //e without extended aux — confirm `text80()` gives
firmware 80-col and lowercase renders natively; `run-iz-compiler-iie-aux`
(`iienh`) for the aux build's paging.
