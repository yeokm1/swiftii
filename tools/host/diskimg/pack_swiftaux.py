#!/usr/bin/env python3
"""tools/host/diskimg/pack_swiftaux.py — pack SWIFTAUX.main + the XLC copy-down
overlay bodies into a single SWIFTAUX.SYSTEM file with a 4-byte header.

step 2 (packed-directory park; replaced slice-2's
fixed-stride park). Companion design doc:
docs/contributing/design/011-extras-lc-in-saturn-aux.md § "Stage 2 refresh —
SWIFTAUX".

File layout (little-endian, unsigned 16-bit):

    offset 0          : main_size_lo, main_size_hi
    offset 2          : park_size_lo, park_size_hi
    offset 4          : main image (main_size B; loads to $2000)
    offset 4+main     : park image (park_size B; the boot-launcher aux loader
                        stages this verbatim into AUX main RAM at AUX_PARK
                        ($2000 aux) via ROM AUXMOVE)

Park image = [directory][bodies]:

  - directory: DIR_ENTRIES (= 25, one per builtin id $0D..$25) entries of
    4 bytes each: off_lo, off_hi, len_lo, len_hi. Entry index =
    builtin_id - BUILTIN_XLC_FIRST. `off` is relative to the park base
    (AUX_PARK), so it already includes the directory size; `len` is the
    body's exact byte length. The runtime trampoline (aux_xlc.s) reads the
    entry for the requested id, then AUXMOVEs exactly `len` bytes from
    AUX_PARK+off down to the STAGING buffer and JSRs it.
  - bodies: the distinct overlay images concatenated in BODY_ORDER, no
    padding. Several builtin ids may share one body (the 13 platform
    builtins fan into the two grouped bodies pmem/pgr); their directory
    entries simply point at the same (off, len).

This replaces the fixed-STRIDE/zero-padded park: bodies can be any size up
to the STAGING hole (__STAGEMAX__), copies are body-sized (no 2 KB-per-call
tax), and there is no padding to ship on disk.

KEEP IN SYNC: DIR_ENTRIES/entry shape here == the directory read in
src/platform/apple2/aux_xlc.s; ID_TO_BODY mirrors opcodes.h
(BUILTIN_XLC_FIRST + the ids); BODY_ORDER == the Makefile's pack argv
order. STAGEMAX here == __STAGEMAX__ in swiftaux-system.cfg.

Usage (one overlay arg per BODY_ORDER entry, in that order):
    pack_swiftaux.py <main.bin> <ovl.asc> <ovl.chr> <ovl.call> <ovl.sip> \
        <ovl.int> <ovl.rml> <ovl.rma> <ovl.con> <ovl.scc> <ovl.nar> \
        <ovl.aln> <ovl.pmem> <ovl.pgr> <out.SYSTEM>
"""

import os
import struct
import sys
import tempfile

MAIN_CEILING = 0xBF00 - 0x2000   # 40,704 B (ProDOS global-page ceiling)
PARK_CEILING = 0xC000 - 0x2000   # 40,960 B (aux main RAM $2000..$BFFF)
STAGEMAX     = 0xBF00 - 0x0400 - 0xB000  # $0B00 = STAGING..C-stack hole
HEADER_SIZE  = 4

BUILTIN_XLC_FIRST = 0x0D
DIR_ENTRIES = 25                 # ids $0D..$25 inclusive
DIR_SIZE    = DIR_ENTRIES * 4    # 100 B

# Distinct copy-down bodies, in the order they are concatenated in the park
# (also the Makefile's pack argv order). One file per distinct dispatcher.
BODY_ORDER = ["asc", "chr", "call", "sip", "int", "rml", "rma", "con",
              "scc", "nar", "aln", "pmem", "pgr"]

# builtin id -> body name. Index into the directory is id - BUILTIN_XLC_FIRST.
# The 13 platform builtins ($18-$24) share the two grouped bodies pmem/pgr;
# str_concat/new_array/arr_len ($0F/$15/$16) are the cold opcode bodies
# evicted from inline-MAIN (step 2). Mirrors opcodes.h.
ID_TO_BODY = {
    0x0D: "asc",   0x0E: "chr",   0x0F: "scc",   0x10: "sip",
    0x11: "int",   0x12: "rml",   0x13: "rma",   0x14: "con",
    0x15: "nar",   0x16: "aln",   0x17: "call",
    0x18: "pmem",  0x19: "pmem",  0x1A: "pmem",  0x1B: "pmem", 0x1C: "pmem",
    0x1D: "pgr",   0x1E: "pgr",   0x1F: "pgr",   0x20: "pgr",  0x21: "pgr",
    0x22: "pgr",   0x23: "pgr",   0x24: "pgr",
    # Text80() ($25) joins the GR group's pgr body.
    0x25: "pgr",
}


def fail(msg):
    print(f"pack_swiftaux: error: {msg}", file=sys.stderr)
    sys.exit(1)


def read_file(path, label):
    try:
        with open(path, 'rb') as f:
            return f.read()
    except OSError as e:
        fail(f"reading {label} {path}: {e}")


def main(argv):
    # argv = prog, main.bin, <one overlay per BODY_ORDER entry>, out.SYSTEM
    expected = 1 + 1 + len(BODY_ORDER) + 1
    if len(argv) != expected:
        ovl_args = " ".join(f"<ovl.{n}>" for n in BODY_ORDER)
        print(f"usage: pack_swiftaux.py <main.bin> {ovl_args} <out.SYSTEM>",
              file=sys.stderr)
        sys.exit(2)
    main_path = argv[1]
    ovl_paths = dict(zip(BODY_ORDER, argv[2:2 + len(BODY_ORDER)]))
    out_path = argv[2 + len(BODY_ORDER)]

    main_bytes = read_file(main_path, "main image")
    if len(main_bytes) > MAIN_CEILING:
        fail(f"main image is {len(main_bytes)} B, exceeds ceiling "
             f"{MAIN_CEILING} B")

    # Read every body; place them after the directory, contiguous, no pad.
    body_off = {}   # name -> offset (relative to park base, incl. directory)
    body_len = {}
    bodies = bytearray()
    off = DIR_SIZE
    summary = []
    for name in BODY_ORDER:
        body = read_file(ovl_paths[name], f"overlay '{name}'")
        if len(body) == 0:
            fail(f"overlay '{name}' {ovl_paths[name]} is empty — did it land "
                 f"in its XLC* segment? (check the WITH_SWIFTAUX #pragma in "
                 f"builtins_xlc.c)")
        if len(body) > STAGEMAX:
            fail(f"overlay '{name}' is {len(body)} B, exceeds the staging "
                 f"hole __STAGEMAX__ {STAGEMAX} B — it would overrun the "
                 f"C-stack when copied to STAGING")
        body_off[name] = off
        body_len[name] = len(body)
        bodies += body
        summary.append(f"{name}@{off}={len(body)}B")
        off += len(body)

    # Build the directory: one entry per builtin id ($0D..$24).
    directory = bytearray()
    for i in range(DIR_ENTRIES):
        bid = BUILTIN_XLC_FIRST + i
        name = ID_TO_BODY.get(bid)
        if name is None:
            fail(f"no body mapped for builtin id ${bid:02X} (idx {i})")
        directory += struct.pack('<HH', body_off[name], body_len[name])
    assert len(directory) == DIR_SIZE

    park = bytes(directory) + bytes(bodies)
    if len(park) > PARK_CEILING:
        fail(f"park is {len(park)} B, exceeds aux main RAM {PARK_CEILING} B")

    header = struct.pack('<HH', len(main_bytes), len(park))
    payload = header + main_bytes + park

    out_dir = os.path.dirname(os.path.abspath(out_path)) or '.'
    fd, tmp_path = tempfile.mkstemp(prefix='.pack_swiftaux.', dir=out_dir)
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

    print(f"pack_swiftaux: {out_path} = {HEADER_SIZE} B header + "
          f"{len(main_bytes)} B main + {len(park)} B park "
          f"({DIR_SIZE} B dir + {len(bodies)} B bodies: {', '.join(summary)})"
          f" = {len(payload)} B total (MAIN headroom "
          f"{MAIN_CEILING - len(main_bytes)} B)")


if __name__ == '__main__':
    main(sys.argv)
