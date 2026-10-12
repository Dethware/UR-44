#!/usr/bin/env python3
"""Read-only sweep of the device's (param, index) address space.

Produced docs/data/scan2d.txt, which established that the wire param id is the memapi
descriptor index and how many instances each group has. Rows = param id, columns = index.
'.' = no reply. Out-of-range indices DO reply, with whatever memory is there (junk values
like 67404224), so interpret columns against the parameter's known range.

Usage: sweep_params.py [last_param] [n_index]     (defaults 0x13f, 12; takes ~20 min)
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools"))
from ur44 import UR44  # noqa: E402

last = int(sys.argv[1], 0) if len(sys.argv) > 1 else 0x13F
n_index = int(sys.argv[2], 0) if len(sys.argv) > 2 else 12

d = UR44()
for p in range(last + 1):
    row = [d.get_raw(p, i, retries=0) for i in range(n_index)]
    if any(v is not None for v in row):
        print(f"{p:3d} 0x{p:03x}: " + " ".join(f"{'.' if v is None else v:>4}" for v in row), flush=True)
