#!/usr/bin/env python3
"""tools/host/diskimg/pack_swiftsat.py — pack SWIFTSAT.main + .xlc with a
4-byte header into a single SWIFTSAT.SYSTEM file.

(2026-05-27). Companion design doc:
docs/contributing/design/011-extras-lc-in-saturn-aux.md.

File layout (little-endian, unsigned 16-bit):

    offset 0          : main_size_lo, main_size_hi
    offset 2          : xlc_size_lo,  xlc_size_hi
    offset 4          : main image (main_size B; loads to $2000)
    offset 4+main     : XLC image  (xlc_size B; loads to Saturn bank 1
                                    at $D000+ via chunked staging)

The main image includes the cc65-standard LC bytes (per swiftsat-
system.cfg's `LC: load = MAIN, run = LC`). cc65's apple2 crt0.s
memcpys those LC bytes to built-in LC bank 2 at startup — the boot
launcher doesn't touch LC at all.

The XLC image is the extras-only code (asc/chr/Int(s)/
struct/switch/etc.) compiled into a separate ld65 segment. It's
empty in stage 1 commits 1+2 (no source files yet gated to XLC);
xlc_size = 0 is the expected value until commit 3+ ships the first
XLC feature.

Earlier dormant doc-011 attempt tried a 3-chunk format (main + lc +
xlc) with `load = LC, run = LC` so the boot launcher could load LC
directly. That doesn't work because cc65's crt0 unconditionally runs
the LC copy regardless of where LC's load address points — and with
load=LC, the source becomes BSS-area garbage. Reverted to the
cc65-standard 2-chunk layout. See docs/contributing/LESSONS.md § "cc65 crt0's LC copy
is unconditional".

Per-chunk ceilings validated against swiftsat-system.cfg:
- main_size: $BF00 - $2000 = 40,704 B (ProDOS global page ceiling).
- xlc_size:  $3000 = 12,288 B (Saturn bank 1).

The script writes the output atomically (via os.replace from a temp
file) so a half-written file can't corrupt a downstream `make disk`.

Usage:
    pack_swiftsat.py <main.bin> <xlc.bin> <output.SYSTEM>
"""

import os
import struct
import sys
import tempfile

# Per swiftsat-system.cfg. Update both files in lockstep.
MAIN_CEILING = 0xBF00 - 0x2000   # 40,704 B
XLC_CEILING  = 0x3000            # 12,288 B
HEADER_SIZE  = 4

def fail(msg):
    print(f"pack_swiftsat: error: {msg}", file=sys.stderr)
    sys.exit(1)

def read_chunk(path, label, ceiling):
    try:
        with open(path, 'rb') as f:
            data = f.read()
    except OSError as e:
        fail(f"reading {label} {path}: {e}")
    if len(data) > ceiling:
        fail(f"{label} {path} is {len(data)} B, exceeds ceiling {ceiling} B")
    return data

def main(argv):
    if len(argv) != 4:
        print("usage: pack_swiftsat.py <main.bin> <xlc.bin> <output.SYSTEM>",
              file=sys.stderr)
        sys.exit(2)
    main_path, xlc_path, out_path = argv[1], argv[2], argv[3]

    main_bytes = read_chunk(main_path, "main image", MAIN_CEILING)
    xlc_bytes  = read_chunk(xlc_path,  "xlc image",  XLC_CEILING)

    header = struct.pack('<HH', len(main_bytes), len(xlc_bytes))
    payload = header + main_bytes + xlc_bytes

    out_dir = os.path.dirname(os.path.abspath(out_path)) or '.'
    fd, tmp_path = tempfile.mkstemp(prefix='.pack_swiftsat.', dir=out_dir)
    try:
        with os.fdopen(fd, 'wb') as f:
            f.write(payload)
        os.replace(tmp_path, out_path)
    except OSError as e:
        try:
            os.unlink(tmp_path)
        except OSError:
            pass
        fail(f"writing {out_path}: {e}")

    print(f"pack_swiftsat: {out_path} = "
          f"{HEADER_SIZE} B header + {len(main_bytes)} B main + "
          f"{len(xlc_bytes)} B xlc = {len(payload)} B total "
          f"(MAIN headroom {MAIN_CEILING - len(main_bytes)} B, "
          f"XLC headroom {XLC_CEILING - len(xlc_bytes)} B)")

if __name__ == '__main__':
    main(sys.argv)
