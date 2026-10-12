#!/usr/bin/env python3
"""Live probe: insert a channel strip (or guitar FX) on an input, watch the meters, remove it again.

FX assign is PJ_COMMAND 0xE / 0xF (macOS MemAPI::SetFxPatch / ResetFxPatch -> ParameterChange::convertFxAssign):
  assign    F0 43 10 3E 14 03 08 01 <fx type> <ch:u16> <insert point> F7
  unassign  F0 43 10 3E 14 03 08 02 <fx type> <ch:u16> 00 F7
  fx type: 0 = channel strip, 1 = guitar FX; insert point: 0 = Pre, 1 = Post
The device picks the strip. Snapshots before/after are diffed so any leftover change is reported.

Usage: probe_fx_assign.py [--input N(1-6)] [--type 0|1] [--point 0|1] [--seconds S]
                          [--set NAME:INDEX=VALUE ...]   (applied after assign, restored before unassign)
"""
import argparse
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools"))
from ur44 import UR44, METER_PAIRS, dec, label, meter_db, parse_param_msg, u16  # noqa: E402
from ur44_params import BY_ID  # noqa: E402


def fx_msg(assign, fx_type, ch, point):
    return [0xF0, 0x43, 0x10, 0x3E, 0x14, 0x03, 0x08, 0x01 if assign else 0x02,
            fx_type, *u16(ch), point if assign else 0, 0xF7]


def show_incoming(d, seconds, tag):
    end = time.monotonic() + seconds
    while (left := end - time.monotonic()) > 0:
        m = d.recv(left)
        if not m:
            break
        p = parse_param_msg(m)
        if p and p[0] in BY_ID:
            print(f"  {tag} notify {BY_ID[p[0]][1]}[{p[1]}] = {label(p[0], p[2])}")
        elif m[1:7] != b"\x43\x10\x3e\x14\x02\x02":
            print(f"  {tag} msg {m.hex(' ').upper()}")


def fx_state(d, ch):
    names = ("ChFxInsPrePatch", "ChFxInsPostPatch", "ChGtrFxInsPrePatch", "ChGtrFxInsPostPatch")
    return {n: label(n, d.get(n, ch)) for n in names}


def diff(a, b):
    out = []
    for pid in sorted(set(a) | set(b)):
        if a.get(pid) != b.get(pid):
            out.append(f"{BY_ID[pid][1]}: {a.get(pid)} -> {b.get(pid)}")
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--input", type=int, default=1)
    ap.add_argument("--type", type=int, default=0, choices=[0, 1])
    ap.add_argument("--point", type=int, default=1, choices=[0, 1])
    ap.add_argument("--seconds", type=float, default=6.0)
    ap.add_argument("--set", action="append", default=[], metavar="NAME:INDEX=VALUE")
    a = ap.parse_args()
    ch = a.input - 1

    d = UR44()
    before = d.snapshot()
    print(f"input {a.input} before:", fx_state(d, ch))

    print(f"assign fx type {a.type} to input {a.input}, point {a.point}")
    d.send(fx_msg(True, a.type, ch, a.point))
    show_incoming(d, 0.5, "after assign:")
    print(f"input {a.input} after assign:", fx_state(d, ch))
    assigned = d.snapshot()
    for line in diff(before, assigned):
        print("  changed by assign:", line[:200])

    saved = []
    for spec in a.set:
        target, value = spec.split("=")
        name, idx = target.split(":")
        old = d.get(name, int(idx))
        saved.append((name, int(idx), old))
        d.set(name, int(idx), int(value))
        print(f"  set {name}[{idx}] {label(name, old)} -> {label(name, d.get(name, int(idx)))}")

    d.meters(True)
    try:
        end = time.monotonic() + a.seconds
        n = 0
        while time.monotonic() < end:
            m = d.recv(0.5)
            if not (m and m[1:7] == b"\x43\x10\x3e\x14\x02\x02"):
                continue
            body = m[7:-1]
            v = [dec(body[i:i + 3]) for i in range(0, len(body) - 2, 3)]
            n += 1
            if n % 4 == 0:
                cols = {k: (v[2 * i], v[2 * i + 1]) for i, k in enumerate(METER_PAIRS)}
                print(f"  IN{a.input} {meter_db(cols[f'IN{a.input}'][0]):>5}  " + "  ".join(
                    f"{k} {x:#06x}/{y:#06x}" for k, (x, y) in cols.items() if k[:2] in ("GR", "CS", "GT")))
    finally:
        d.meters(False)
        for name, idx, old in reversed(saved):
            d.set(name, idx, old)
            print(f"  restored {name}[{idx}] = {label(name, d.get(name, idx))}")

    print(f"unassign fx type {a.type} from input {a.input}")
    d.send(fx_msg(False, a.type, ch, 0))
    show_incoming(d, 0.5, "after unassign:")
    print(f"input {a.input} after unassign:", fx_state(d, ch))
    after = d.snapshot()
    left = diff(before, after)
    print("state identical to before" if not left else "LEFTOVER CHANGES:\n  " + "\n  ".join(left))


if __name__ == "__main__":
    main()
