"""Smoke test: prove py65 is installed and a trivial 6502 program runs.

only checks that the simulator works at all. Will add
real VM-dispatch tests. The point of this file is that the import in
`runner.py` succeeds (= py65 is in PATH) and a tiny program executes
the way a 6502 would.
"""

from __future__ import annotations

from helpers import load_bytes, make_mpu


def test_smoke_lda_sta() -> None:
    """LDA #$41 ; STA $0400 ; BRK — load 'A', store at the start of the
    text page, halt. Verifies LDA and STA both work and that the BRK
    catches us correctly.
    """
    mpu = make_mpu()
    program = bytes([
        0xA9, 0x41,         # LDA #$41
        0x8D, 0x00, 0x04,   # STA $0400
        0x00,               # BRK
    ])
    load_bytes(mpu, 0x0800, program)
    mpu.pc = 0x0800

    # Step at most 8 instructions; the smoke program is 3.
    for _ in range(8):
        if mpu.memory[mpu.pc] == 0x00:
            break
        mpu.step()
    else:
        raise AssertionError("did not reach BRK")

    assert mpu.a == 0x41, f"expected A=0x41, got {mpu.a:#x}"
    assert mpu.memory[0x0400] == 0x41, (
        f"expected $0400=0x41, got {mpu.memory[0x0400]:#x}"
    )
