# 012 - Phase 10 platform built-ins: placement and slice 1

## Problem

Phase 10 ("Apple II Platform APIs") wants Swift-style access to
Apple II hardware - clear the screen, position the cursor, read and
write raw memory (soft switches, the speaker, paddles), and later
low-res graphics. The roadmap's stated intent is that platform
built-ins land in **both** the lite binaries and the extras binaries
together, so user programs stay portable across the lite/extras boundary.

That intent collides with the budget reality. The lite binary has
~115 B of ProDOS-ceiling headroom. `peek`/`poke` alone measured 163 B
in MAIN when last shipped during Phase 8; `home`
measured ~193 B, `htab`+`vtab` ~250 B. None of the Phase 10 screen/
memory built-ins fit in lite as MAIN-resident `OP_CALL_BUILTIN`
branches. The Phase 9 close lesson was explicit: *runtime* features
(platform built-ins) can move to XLC even though compiler-side codegen
can't - but XLC exists only on SWIFTSAT.

So: where do the Phase 10 built-ins live, and what ships first?

## Proposal

Ship Phase 10 platform built-ins as **SWIFTSAT-only XLC built-ins**,
following the exact precedent set by the Phase 9 `asc`/`chr`/`Int(s)`/
array-method work: a dispatcher in `src/vm/builtins_xlc.c` (placed in
the `XLC` segment / Saturn bank 1 on SWIFTSAT, normal `CODE` on host),
a `.word` slot in `xlc_table.s`, and a parser branch in
`src/compiler/builtin_calls.c` gated `WITH_SWIFTSAT || !__CC65__`. They
are absent on lite + legacy SWIFTIIX (compile error: `undeclared
name`).

Slice 1 (this commit): **`home()`, `peek(addr)`, `poke(addr, value)`**.
This is the smallest demo-visible slice - `home()` clears the screen,
`poke(49200, 0)` clicks the speaker for free, `peek`/`poke` are the
foundational memory primitive everything else can be expressed in.

## Detailed design

New built-in ids (opcodes.h), in the XLC range (≥ `BUILTIN_XLC_FIRST`,
which is what the vm.c dispatcher keys on to route through the XLC JMP
table - the old MAIN reservations $08/$0B/$0C can't be used because
they're below that threshold):

- `BUILTIN_HOME 0x18` - XLC table slot 11, `$D02C`
- `BUILTIN_PEEK 0x19` - XLC table slot 12, `$D030`
- `BUILTIN_POKE 0x1A` - XLC table slot 13, `$D034`

Dispatchers (`builtins_xlc.c`, inside the `code-name "XLC"` pragma):

- `xlc_home_dispatch(argc)` - argc 0; `platform_clear_screen()`;
  pushes T_NIL. Stack `( -- nil )`.
- `xlc_peek_dispatch(argc)` - argc 1; pops T_INT addr; on cc65
  dereferences `*(volatile unsigned char *)addr`, on host returns 0;
  pushes T_INT 0..255. Stack `( addr -- byte )`.
- `xlc_poke_dispatch(argc)` - argc 2; pops T_INT addr + T_INT value;
  on cc65 writes the low byte, on host no-op; pushes T_NIL. Stack
  `( addr value -- nil )`.

Raw memory access is safe from XLC: main RAM ($0000–$BFFF, soft
switches at $C0xx) is always visible regardless of the Saturn
bank-select state - only $D000–$FFFF is paged, and that's the XLC
bank itself (peeking/poking there is documented user error, not our
concern). The host stubs keep the test suite deterministic and avoid
UB from dereferencing arbitrary pointers under ASan.

Parser: `home` is a zero-arg call → `emit_builtin(HOME, 0)`,
`comp_set_expr_ctype(CT_VOID)`. `peek` reuses `parse_unary_builtin`
(CT_INT → CT_INT). `poke` parses two CT_INT args and emits with
argc 2, result CT_VOID. Both `home()` and `poke()` work as bare
statements because `statements.c` and `pratt.c` both route bare
`IDENT(` through `try_compile_builtin_call`.

REPL void-result echo: `home()`/`poke()` return nil, and the REPL's
bare-statement auto-print (`emit_expr_stmt_end`) would otherwise print
that nil as a spurious `nil` line after the side effect. The hook now
suppresses the echo when `comp_get_expr_ctype() == CT_VOID`, gated to
`WITH_SWIFTSAT || !__CC65__` so the void platform built-ins (which
don't exist on lite) leave lite + legacy SWIFTIIX byte-identical.
`peek` returns CT_INT so it still echoes its value at the REPL.

## Alternatives considered

1. **MAIN, all binaries (use the reserved $08/$0B/$0C ids).** Keeps the
   portability invariant - peek/poke/home exist everywhere. Rejected
   for slice 1: doesn't fit lite (163 B vs 115 B headroom) without a
   budget sweep or an `OP_CALL_BUILTIN` restructure, both of which are
   multi-session efforts that would block the demo-visible work. Still
   the right long-term home for the *primitive* (peek/poke) if a sweep
   opens runway - see Migration.

2. **Hybrid (peek/poke in MAIN after a lite sweep; richer screen
   built-ins in XLC).** Best long-term portability of the primitive,
   but front-loads a budget sweep before any Phase 10 feature ships.
   Deferred - revisit if lite users need raw memory access.

3. **Do nothing / keep deferring.** Phase 10 is the platform-API phase;
   not starting it isn't an option, and the XLC path is proven.

## Cost

- **Memory cost**: SWIFTSAT MAIN 821 → 366 B headroom (−455 B: three
  parser branches + the CT_VOID echo check); XLC 3827 → 4259 B
  (+432 B: three dispatchers + three table slots). Lite + legacy
  SWIFTIIX **byte-identical** (40589 / 40594 B). Host build grows by
  the same dispatchers (no budget there).
- **Performance cost**: one extra bus-switch per platform-built-in call
  (the XLC trampoline), same as every other XLC built-in. These are
  cold calls, not hot-loop opcodes - negligible.
- **Code complexity cost**: none new - slice 1 reuses the Phase 9 XLC
  machinery wholesale. The only new wrinkle is the CT_VOID echo
  suppression, a 3-line gated change in `emit_expr_stmt_end`.
- **Schedule cost**: ~half a session for slice 1.

## Migration

Clean addition. No existing bytecode, test, or program changes
behavior. `home`/`peek`/`poke` are new names that previously errored as
`undeclared name` and still do on lite + legacy SWIFTIIX.

Forward path: if a lite budget sweep or `OP_CALL_BUILTIN` restructure
later frees ≥ ~200 B in MAIN, the peek/poke *primitive* can be promoted
to MAIN ids ($0B/$0C, still reserved) so it lands in all binaries,
restoring the portability invariant for the foundational case. The
richer screen/GR built-ins (home/htab/vtab/gr/color/plot/…) stay XLC.

## Open questions

- Each new platform-built-in slice adds a parser branch to SWIFTSAT
  MAIN (~150 B/slice). With 366 B left, roughly two more slices fit
  before MAIN needs attention - at which point the parser recognition
  can move into a MAIN helper (cf. `try_compile_array_method`) or the
  name table can be restructured. Not a slice-1 blocker.

## Decision

Implemented with XLC SWIFTSAT-only placement; slice 1 = `home` +
`peek`/`poke`.
