"""Shared py65 setup helpers for sim tests.

only exposes `make_mpu()`, which constructs a fresh py65 6502
with 64 KB of RAM and returns it. Will add bytecode-loading
helpers on top of this.
"""

from __future__ import annotations

from py65.devices.mpu6502 import MPU


def make_mpu() -> MPU:
    """Return a fresh 6502 with a flat 64 KB memory."""
    mpu = MPU()
    mpu.memory = [0x00] * 0x10000
    return mpu


def load_bytes(mpu: MPU, addr: int, data: bytes) -> None:
    """Load `data` into `mpu.memory` starting at `addr`."""
    for i, b in enumerate(data):
        mpu.memory[addr + i] = b


def step_until_brk(mpu: MPU, max_cycles: int = 100_000) -> int:
    """Step until the BRK opcode ($00) executes. Returns the PC at BRK.

    Raises RuntimeError if `max_cycles` elapses first.
    """
    for _ in range(max_cycles):
        opcode = mpu.memory[mpu.pc]
        if opcode == 0x00:
            return mpu.pc
        mpu.step()
    raise RuntimeError("step_until_brk: budget exhausted")
