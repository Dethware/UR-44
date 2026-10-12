#!/usr/bin/env python3
"""Extract the per-parameter display labels from memapi_ur44.dll (32-bit, V2.1.0).

memapi keeps PTR_PTR_10025778[param_id] -> {char **labels, char *unit}; the display
text of raw value v is labels[v] (FUN_1000ada0). Exception: the shared integer table
at 0x10022438 is indexed by v - min. Writes JSON {id: {name, min, max, unit, labels}} (tools/ur44_labels.json); labels[0] is the label of min.
"""
import json
import struct
import sys

BASE = 0x10000000
DESC_TABLE = 0x10022118
LABEL_TABLE = 0x10025778
INT_TABLE = 0x10022438
N = 0xB1

data = open(sys.argv[1], "rb").read()
u32 = lambda va: struct.unpack_from("<I", data, va - BASE)[0]
i32 = lambda va: struct.unpack_from("<i", data, va - BASE)[0]


def cstr(va):
    o = va - BASE
    return data[o:data.index(b"\0", o)].decode("latin1")


out = {}
for pid in range(N):
    desc = u32(DESC_TABLE + 4 * pid)
    name, mx, mn = cstr(u32(desc + 0x28)), i32(desc + 0x18), i32(desc + 0x1C)
    ent = u32(LABEL_TABLE + 4 * pid)
    labels, unit = None, ""
    if ent:
        tab, unit_p = u32(ent), u32(ent + 4)
        unit = cstr(unit_p) if unit_p else ""
        if tab:
            off = mn if tab == INT_TABLE else 0
            labels = [cstr(u32(tab + 4 * (v - off))) for v in range(mn, mx + 1)]
    out[pid] = {"name": name, "min": mn, "max": mx, "unit": unit, "labels": labels}

json.dump(out, sys.stdout, indent=1)
