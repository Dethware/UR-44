#!/usr/bin/env python3
"""Compare the macOS MemAPI 2.3.0 parameter model with Windows memapi_ur44.dll 2.1.0.

The macOS bundle (dspMixFx_UR44.app/Contents/PlugIns/MemAPI_UR44.bundle) keeps its symbols:
  _item<Name>          one 0x38-byte descriptor per parameter (same first 9 int32 fields as Windows)
  PJ::prm (__ZN2PJL3prmE)   id -> {char **labels, char *unit}, like Windows PTR_PTR_10025778
  PJ::panTable / PJ::gtrOutputLevelTable   label tables indexed by value - min
Uses llvm-lipo / llvm-nm for the x86_64 slice. Prints descriptor and label differences.

Usage: compare_mac_memapi.py MemAPI_UR44(mac, universal) memapi_ur44.dll(win) ur44_labels.json
"""
import json
import struct
import subprocess
import sys
import tempfile

mac_path, win_path, labels_path = sys.argv[1:4]
FIELDS = ["id", "type", "kind", "unk", "size", "group", "max", "min", "default"]

with tempfile.NamedTemporaryFile() as tmp:
    subprocess.run(["llvm-lipo", "-thin", "x86_64", mac_path, "-output", tmp.name], check=True)
    mac = open(tmp.name, "rb").read()
    nm = subprocess.run(["llvm-nm", "--defined-only", tmp.name], check=True, capture_output=True, text=True).stdout
syms = {n: int(a, 16) for a, _, n in (l.split() for l in nm.splitlines() if len(l.split()) == 3)}
# this Mach-O slice maps VM addresses 1:1 to file offsets (checked: __TEXT/__DATA vmaddr == fileoff)
q = lambda a: struct.unpack_from("<Q", mac, a)[0]
mstr = lambda a: mac[a:mac.index(b"\0", a)].decode("latin1")

mac_items = {}
for n, a in syms.items():
    if n.startswith("_item"):
        f = struct.unpack_from("<9i", mac, a)
        mac_items[f[0] & 0xFFFF] = (n[5:], f)

win = open(win_path, "rb").read()
B = 0x10000000
u32 = lambda va: struct.unpack_from("<I", win, va - B)[0]
wstr = lambda va: win[va - B:win.index(b"\0", va - B)].decode("latin1")
win_items = {}
for pid in range(0xB1):
    p = u32(0x10022118 + 4 * pid)
    win_items[pid] = (wstr(u32(p + 0x28)), struct.unpack_from("<9i", win, p - B))

diffs = 0
for pid in sorted(set(mac_items) | set(win_items)):
    if pid not in mac_items or pid not in win_items:
        print(f"0x{pid:02x}: only on {'mac' if pid in mac_items else 'win'}")
        diffs += 1
        continue
    (mn, mf), (wn, wf) = mac_items[pid], win_items[pid]
    if mn != wn:
        print(f"0x{pid:02x}: renamed {wn} -> {mn}")
        diffs += 1
    for i, (a, b) in enumerate(zip(wf, mf)):
        if a != b:
            print(f"0x{pid:02x} {wn}: {FIELDS[i]} {a} -> {b}")
            diffs += 1

PRM, PAN, GTR = syms["__ZN2PJL3prmE"], syms["__ZN2PJL8panTableE"], syms["__ZN2PJL19gtrOutputLevelTableE"]
win_labels = json.load(open(labels_path))
for pid in range(0xB1):
    if pid == 0x42:  # FxRevTime: computed from room size / type on both platforms
        continue
    _, f = mac_items[pid]
    lo, hi = f[7], f[6]
    ent, labels, unit = q(PRM + 8 * pid), None, ""
    if ent:
        tab, up = q(ent), q(ent + 8)
        unit = mstr(up) if up else ""
        if tab:
            off = lo if tab in (PAN, GTR) else 0
            labels = [mstr(q(tab + 8 * (v - off))) for v in range(lo, hi + 1)]
    w = win_labels[str(pid)]
    if (labels, unit) != (w["labels"], w["unit"]):
        print(f"0x{pid:02x}: label table differs (unit {w['unit']!r} -> {unit!r})")
        diffs += 1

print(f"{len(mac_items)} mac / {len(win_items)} win descriptors, {diffs} difference(s)")
