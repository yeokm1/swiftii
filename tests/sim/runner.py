#!/usr/bin/env python3
"""tests/sim/runner.py — discovers and runs the py65 sim-test suite.

has one smoke test (smoke_test.py) that just confirms py65 is
installed and a trivial 6502 program runs to BRK with the right register
state. Will add bytecode VM tests that exercise dispatch.
"""

from __future__ import annotations

import importlib.util
import sys
import traceback
from pathlib import Path


def main() -> int:
    here = Path(__file__).resolve().parent
    test_files = sorted(p for p in here.glob("*_test.py"))

    if not test_files:
        print("sim: no tests discovered")
        return 0

    failed = 0
    total = 0

    for path in test_files:
        spec = importlib.util.spec_from_file_location(path.stem, path)
        if spec is None or spec.loader is None:
            continue
        mod = importlib.util.module_from_spec(spec)
        try:
            spec.loader.exec_module(mod)
        except Exception:
            failed += 1
            total += 1
            print(f"FAIL {path.stem} (import failed)")
            traceback.print_exc()
            continue
        for name in dir(mod):
            if not name.startswith("test_"):
                continue
            fn = getattr(mod, name)
            if not callable(fn):
                continue
            total += 1
            try:
                fn()
                print(f"ok   {path.stem}::{name}")
            except Exception:
                failed += 1
                print(f"FAIL {path.stem}::{name}")
                traceback.print_exc()

    print(f"--- {total} sim test(s), {failed} failed")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
