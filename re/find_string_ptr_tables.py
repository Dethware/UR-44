#!/usr/bin/env python3
"""List arrays of pointers to short printable C strings in a 32-bit PE image.

This is how the memapi display-label tables were found ("-INF", "-74.00", ... "+6.00"):
the compiler merges duplicate strings, so the real lookup tables are arrays of char*.
Prints each run's VA, length and first/last strings.

Usage: find_string_ptr_tables.py image.dll [min_run]
Assumes section VA == file offset + image base (true for memapi_ur44.dll; check `objdump -h`).
"""
import struct
import sys

path = sys.argv[1]
min_run = int(sys.argv[2]) if len(sys.argv) > 2 else 3
d = open(path, "rb").read()
base = struct.unpack_from("<I", d, struct.unpack_from("<I", d, 0x3C)[0] + 0x34)[0]  # OptionalHeader.ImageBase


def cstr(va):
    o = va - base
    if not 0 <= o < len(d):
        return None
    end = d.find(b"\0", o, o + 24)
    if end <= o:
        return None
    s = d[o:end]
    return s.decode("latin1") if all(32 <= c < 127 for c in s) else None


run = []
for off in range(0, len(d) - 4, 4):
    s = cstr(struct.unpack_from("<I", d, off)[0])
    if s is not None:
        run.append((off, s))
        continue
    if len(run) >= min_run:
        labels = [x for _, x in run]
        print(f"0x{run[0][0] + base:08x} n={len(run):4d}  {labels[:3]} ... {labels[-3:]}")
    run = []
