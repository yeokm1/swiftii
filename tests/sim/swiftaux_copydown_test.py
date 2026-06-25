"""SWIFTAUX copy-down dispatcher proof (py65 sim).

The make-or-break risk for SWIFTAUX is position independence: cc65 emits
position-dependent code, and the aux variant copies a dispatcher body
*down* into a fixed main-RAM STAGING buffer ($B000) and JSRs it there.
This test proves the copied-down bodies run correctly against
MAIN-resident data — loading the real SWIFTAUX MAIN image at $2000 and
each overlay at STAGING (simulating the copy the boot-launcher loader +
aux.s trampoline do via ROM AUXMOVE, which py65 can't model), seeding VM
state, and JSRing the dispatcher.

Covered: asc (worker co-located; XLCASC), chr (single dispatcher; XLCCHR),
Int(_ s:) (step 1; worker co-located, XLCINT), and peek via the GROUPED
pmem overlay (step 2; offset-0 stub -> xlc_pmem_dispatch -> fan out by
xlc_builtin_id -> xlc_peek_dispatch). The trampoline's directory read +
AUXMOVE switching + the boot-launcher aux loader are emulator-verified (py65
has flat memory, no aux banks); these tests prove the copied-down BODIES
run correctly at STAGING against MAIN data, including the step-2
group-switch.

Build first with `make apple2-swiftaux`; this SKIPs (printing a notice)
if the artifacts are absent so `make sim` never breaks on CI ordering.
Companion: docs/contributing/design/012 § "Stage 2 refresh — SWIFTAUX".
"""

from __future__ import annotations

from pathlib import Path

from helpers import load_bytes, make_mpu

_ROOT = Path(__file__).resolve().parents[2]
_AUX = _ROOT / "build" / "apple2" / "swiftaux"
_MAIN = _AUX / "SWIFTAUX.main"
_LBL = _AUX / "swiftaux.lbl"
_OVL = {"asc": _AUX / "SWIFTAUX.ovl.asc", "chr": _AUX / "SWIFTAUX.ovl.chr",
        "int": _AUX / "SWIFTAUX.ovl.int", "pmem": _AUX / "SWIFTAUX.ovl.pmem",
        "pgr": _AUX / "SWIFTAUX.ovl.pgr"}

# From src/common/types.h / errors.h / opcodes.h.
T_STR = 0x10
T_INT = 0x02
T_OPT_NIL = 0x20
T_NIL = 0x00
SE_OK = 0
BUILTIN_PEEK = 0x19      # a member of the grouped pmem overlay
BUILTIN_TEXT80 = 0x25 # newest pgr-group member
POOL_SLOT_0 = 0          # "hello, world" in string_pool.c; asc -> 'h'
EXPECT_ASC = ord("h")    # 104

CC65_SP = 0x0080         # cc65 software-stack pointer (ZEROPAGE seg start)
CSTACK_TOP = 0x0800      # safe C-stack area for the JSR'd body
RET_BRK = 0x0300         # dispatcher's final RTS lands on a BRK here


def _parse_lbl(path: Path) -> dict[str, int]:
    syms: dict[str, int] = {}
    for line in path.read_text().splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[0] == "al" and parts[2].startswith("."):
            try:
                syms[parts[2][1:]] = int(parts[1], 16)
            except ValueError:
                pass
    return syms


def _have_build() -> bool:
    return _MAIN.exists() and _LBL.exists() and all(p.exists() for p in _OVL.values())


def _run_to_brk(mpu, addr, argc):
    """JSR `addr` with A=argc, returning to a sentinel BRK; step to it.
    Returns the A register (the routine's return value)."""
    mpu.memory[RET_BRK] = 0x00  # BRK
    mpu.sp = 0xFF
    mpu.memory[0x0100 + mpu.sp] = ((RET_BRK - 1) >> 8) & 0xFF
    mpu.sp -= 1
    mpu.memory[0x0100 + mpu.sp] = (RET_BRK - 1) & 0xFF
    mpu.sp -= 1
    mpu.a = argc
    mpu.pc = addr
    for _ in range(200_000):
        if mpu.memory[mpu.pc] == 0x00:
            break
        mpu.step()
    else:
        raise AssertionError(f"routine ${addr:04X} did not return to the BRK")
    assert mpu.pc == RET_BRK, f"returned to ${mpu.pc:04X}"
    return mpu.a


def _run_dispatcher(syms, overlay_name, seed_stack, argc=1, builtin_id=None):
    """Load MAIN + the named overlay at STAGING, initialise the heap (JSR
    the real _heap_reset, so chr's heap_alloc has a valid bump pointer),
    seed s_stack[0] via `seed_stack(mpu, s_stack)`, then enter at STAGING
    itself — exactly as the aux_xlc_call trampoline does (`jsr STAGING`)
    after copying the overlay down. STAGING's offset 0 is the aux_table.s
    `jmp <dispatcher>` stub, so this exercises stub → dispatcher. For the
    grouped platform overlays (pmem/pgr) the entry switches on
    `xlc_builtin_id`, which vm.c stashes before the call — pass `builtin_id`
    to seed it. Returns the mpu after it RTSes (asserting SE_OK)."""
    staging = syms["__STAGING__"]
    ovl = _OVL[overlay_name].read_bytes()

    mpu = make_mpu()
    load_bytes(mpu, 0x2000, _MAIN.read_bytes())
    load_bytes(mpu, staging, ovl)
    mpu.memory[CC65_SP] = CSTACK_TOP & 0xFF
    mpu.memory[CC65_SP + 1] = (CSTACK_TOP >> 8) & 0xFF

    # Offset 0 must be the JMP stub ($4C) so `jsr STAGING` reaches the
    # dispatcher (the trampoline's core assumption).
    assert mpu.memory[staging] == 0x4C, (
        f"overlay '{overlay_name}' offset 0 is ${mpu.memory[staging]:02X}, "
        f"expected $4C (jmp stub)"
    )

    _run_to_brk(mpu, syms["_heap_reset"], 0)  # init the bump heap

    if builtin_id is not None:
        mpu.memory[syms["_xlc_builtin_id"]] = builtin_id
    seed_stack(mpu, syms["_s_stack"])
    mpu.memory[syms["_vm_sp"]] = 1

    err = _run_to_brk(mpu, staging, argc)  # enter at STAGING, like the trampoline
    assert err == SE_OK, f"err = {err}, expected SE_OK"
    return mpu


def test_asc_copydown() -> None:
    if not _have_build():
        print(f"SKIP swiftaux_copydown: not built (make apple2-swiftaux) — {_AUX}")
        return
    syms = _parse_lbl(_LBL)
    s = syms["_s_stack"]

    def seed(mpu, s_stack):
        mpu.memory[s_stack + 0] = T_STR
        mpu.memory[s_stack + 1] = POOL_SLOT_0 & 0xFF
        mpu.memory[s_stack + 2] = (POOL_SLOT_0 >> 8) & 0xFF

    mpu = _run_dispatcher(syms, "asc", seed)
    assert mpu.memory[syms["_vm_sp"]] == 1
    assert mpu.memory[s] == T_INT, f"tag {mpu.memory[s]:#x}, expected T_INT"
    assert mpu.memory[s + 1] == EXPECT_ASC, (
        f"asc -> {mpu.memory[s + 1]}, expected {EXPECT_ASC} ('h')"
    )


def test_chr_copydown() -> None:
    if not _have_build():
        print(f"SKIP swiftaux_copydown: not built (make apple2-swiftaux) — {_AUX}")
        return
    syms = _parse_lbl(_LBL)
    s = syms["_s_stack"]

    def seed(mpu, s_stack):
        mpu.memory[s_stack + 0] = T_INT
        mpu.memory[s_stack + 1] = 65  # 'A'
        mpu.memory[s_stack + 2] = 0

    mpu = _run_dispatcher(syms, "chr", seed)
    assert mpu.memory[syms["_vm_sp"]] == 1
    # chr allocates a 1-byte heap string and pushes a T_STR.
    assert mpu.memory[s] == T_STR, f"tag {mpu.memory[s]:#x}, expected T_STR"


def test_int_copydown() -> None:
    """Int(_ s:) at slot 4 (XLCINT, worker co-located) — slice 3 step 1.
    Seeds pool slot 0 ("hello, world", non-numeric) so the worker's parse
    fails and the dispatcher pushes T_OPT_NIL (a valid `nil` result, not
    an error). Proves a >slice-2 slot copies down, the co-located worker
    runs, and the str_bytes call back into MAIN resolves."""
    if not _have_build():
        print(f"SKIP swiftaux_copydown: not built (make apple2-swiftaux) — {_AUX}")
        return
    syms = _parse_lbl(_LBL)
    s = syms["_s_stack"]

    def seed(mpu, s_stack):
        mpu.memory[s_stack + 0] = T_STR
        mpu.memory[s_stack + 1] = POOL_SLOT_0 & 0xFF
        mpu.memory[s_stack + 2] = (POOL_SLOT_0 >> 8) & 0xFF

    mpu = _run_dispatcher(syms, "int", seed)
    assert mpu.memory[syms["_vm_sp"]] == 1
    assert mpu.memory[s] == T_OPT_NIL, (
        f"tag {mpu.memory[s]:#x}, expected T_OPT_NIL (invalid parse -> nil)"
    )


def test_pmem_group_copydown() -> None:
    """peek($0320) via the GROUPED pmem overlay (slice 3 step 2). Unlike the
    per-body overlays, pmem's offset-0 stub jumps to xlc_pmem_dispatch, which
    fans out by xlc_builtin_id to the right member dispatcher — here
    xlc_peek_dispatch. Proves the group-switch + a member run correctly when
    copied down to STAGING (the new step-2 dispatch shape)."""
    if not _have_build():
        print(f"SKIP swiftaux_copydown: not built (make apple2-swiftaux) — {_AUX}")
        return
    syms = _parse_lbl(_LBL)
    s = syms["_s_stack"]
    probe_addr = 0x0320      # scratch byte, clear of MAIN/stack/RET_BRK
    probe_val = 0x42

    def seed(mpu, s_stack):
        mpu.memory[probe_addr] = probe_val
        mpu.memory[s_stack + 0] = T_INT
        mpu.memory[s_stack + 1] = probe_addr & 0xFF
        mpu.memory[s_stack + 2] = (probe_addr >> 8) & 0xFF

    mpu = _run_dispatcher(syms, "pmem", seed, builtin_id=BUILTIN_PEEK)
    assert mpu.memory[syms["_vm_sp"]] == 1
    assert mpu.memory[s] == T_INT, f"tag {mpu.memory[s]:#x}, expected T_INT"
    assert mpu.memory[s + 1] == probe_val, (
        f"peek -> {mpu.memory[s + 1]}, expected {probe_val}"
    )


def test_pgr_text80_copydown() -> None:
    """text80() ($25) via the GROUPED pgr overlay. The
    newest pgr member; proves a directory idx past the original $24 end
    (idx 24) copies down and the group-switch reaches xlc_text80_dispatch.
    In the flat py65 sim MACHID ($BF98) reads 0, so platform_text80()'s
    card probe is false and it no-ops (no `JSR $C300`); the dispatcher just
    pushes nil. Stack ( -- nil )."""
    if not _have_build():
        print(f"SKIP swiftaux_copydown: not built (make apple2-swiftaux) — {_AUX}")
        return
    syms = _parse_lbl(_LBL)
    s = syms["_s_stack"]

    def seed(mpu, s_stack):
        mpu.memory[0xBF98] = 0x00   # MACHID: no 80-col card -> text80 no-ops
        mpu.memory[s_stack + 0] = T_INT  # seed garbage; dispatcher overwrites
        mpu.memory[s_stack + 1] = 0
        mpu.memory[s_stack + 2] = 0

    # argc=0: text80 pushes (does not replace in place), so vm_sp 1->2 and
    # the nil lands in slot 1 (Value is 3 bytes: tag, lo, hi).
    mpu = _run_dispatcher(syms, "pgr", seed, argc=0, builtin_id=BUILTIN_TEXT80)
    assert mpu.memory[syms["_vm_sp"]] == 2, f"vm_sp {mpu.memory[syms['_vm_sp']]}, expected 2"
    assert mpu.memory[s + 3] == T_NIL, f"tag {mpu.memory[s + 3]:#x}, expected T_NIL"
