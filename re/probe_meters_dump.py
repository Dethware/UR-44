#!/usr/bin/env python3
"""Read-only live probes: level meters and bulk dump request.

  meters  : send meter start (F0 43 30 3E 14 02 02 32 7F F7), capture for N seconds, send stop
  dump N  : send bulk dump request N (F0 43 20 3E 14 01 01 <N:u32> F7), capture the reply,
            verify length / checksum and unpack the 7-bit payload into OUT (default dump_<N>.bin)

Neither writes device memory. Usage: probe_meters_dump.py meters [seconds] | dump N [OUT]
"""
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools"))
from ur44 import UR44, dec, u32  # noqa: E402


def unpack7(data):
    """Inverse of BulkDump::convertTo7bit: groups of 7 low-bit bytes followed by one MSB byte."""
    out = bytearray()
    for g in range(0, len(data) - len(data) % 8, 8):
        msb = data[g + 7]
        for k in range(7):
            out.append(data[g + k] | ((msb << (k + 1)) & 0x80))
    return bytes(out)


def capture(d, seconds):
    msgs, end = [], time.monotonic() + seconds
    while (left := end - time.monotonic()) > 0:
        m = d.recv(left)
        if m:
            msgs.append(m)
    return msgs


def meters(d, seconds):
    d.send([0xF0, 0x43, 0x30, 0x3E, 0x14, 0x02, 0x02, 0x32, 0x7F, 0xF7])
    try:
        msgs = capture(d, seconds)
    finally:
        d.send([0xF0, 0x43, 0x30, 0x3E, 0x14, 0x02, 0x02, 0x32, 0x00, 0xF7])
    tail = capture(d, 0.5)
    frames = [m for m in msgs if m[1:6] == b"\x43\x10\x3e\x14\x02" and m[6] == 0x02]
    other = [m for m in msgs if m not in frames]
    print(f"{len(msgs)} messages in {seconds}s: {len(frames)} meter frames, {len(other)} other; "
          f"{len(tail)} after stop")
    if frames:
        lens = sorted({len(f) for f in frames})
        print(f"frame lengths: {lens}; rate ≈ {len(frames) / seconds:.1f}/s")
        for f in frames[:3] + frames[-2:]:
            body = f[7:-1]
            vals = [dec(body[i:i + 3]) for i in range(0, len(body) - len(body) % 3, 3)]
            print(f"  {len(vals)} values: {vals}  (+{len(body) % 3} trailing bytes)")
    for m in other[:5]:
        print("  other:", m.hex(" ").upper())


def dump(d, n, out):
    d.send([0xF0, 0x43, 0x20, 0x3E, 0x14, 0x01, 0x01, *u32(n), 0xF7])
    msgs = capture(d, 3.0)
    print(f"{len(msgs)} message(s)")
    for m in msgs:
        if m[1:4] != b"\x43\x00\x3e" or m[6] != 0x14:
            print("  non-bulk:", m[:24].hex(" ").upper(), "…" if len(m) > 24 else "")
            continue
        ln = (m[4] << 7) | m[5]
        body = m[7:-2]                  # from the "01 01" up to (not including) the checksum
        cs = m[-2]
        calc = -sum(body) & 0x7F
        n_id = dec(m[9:14])
        data7 = m[14:-2]
        print(f"  bulk: total {len(m)} bytes, len field {ln} (body {len(body)}), id {n_id}, "
              f"checksum {cs:#04x} calc {calc:#04x} {'OK' if cs == calc else 'MISMATCH'}, "
              f"data7 {len(data7)} bytes")
        raw = unpack7(data7)
        path = out or f"dump_{n_id}.bin"
        with open(path, "wb") as f:
            f.write(raw)
        print(f"  unpacked {len(raw)} bytes -> {path}")
        print("  head:", raw[:64].hex(" "))


def main():
    d = UR44()
    if sys.argv[1] == "meters":
        meters(d, float(sys.argv[2]) if len(sys.argv) > 2 else 2.0)
    elif sys.argv[1] == "dump":
        dump(d, int(sys.argv[2], 0), sys.argv[3] if len(sys.argv) > 3 else None)


if __name__ == "__main__":
    main()
