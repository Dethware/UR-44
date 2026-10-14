#!/usr/bin/env python3
"""Live probe: write a Sweet Spot morph table to the UR44 (bulk dump type 2, "MorphingTable").

  probe_morph_table.py noop                 re-send the device's current table (must change nothing)
  probe_morph_table.py load STRIP FILE.ssd  load a .ssd preset into strip 1-4 (name taken from the file name)

The payload is the self-describing chunk format of the state dump, with the morph groups only:
MorphSystemId/MorphSystemName (4 strips) and the 17 Morph* fields (4 strips x 9 points).
"""
import os
import struct
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools"))
from ur44 import UR44, u32  # noqa: E402
from ur44_dump import decode, encode  # noqa: E402

MORPH_IDS = list(range(0x53, 0x64))


def pack7(data):
    out = bytearray()
    for g in range(0, len(data), 7):
        chunk = data[g:g + 7].ljust(7, b"\0")
        msb = 0
        for k, b in enumerate(chunk):
            out.append(b & 0x7F)
            msb |= (b & 0x80) >> (k + 1)
        out.append(msb)
    return bytes(out)


def bulk_message(dump_type, payload):
    body = bytes([0x01, 0x01, *u32(dump_type)]) + pack7(payload)
    cs = -sum(body) & 0x7F
    return bytes([0xF0, 0x43, 0x00, 0x3E, (len(body) >> 7) & 0x7F, len(body) & 0x7F, 0x14]) + body + bytes([cs, 0xF7])


def read_ssd(path):
    d = open(path, "rb").read()
    assert d[:4] == b"SSMD", "not an .ssd file"
    ident = d[16:32].split(b"\0")[0]
    points = [struct.unpack_from(">20H", d, 0x20 + 40 * p)[:17] for p in range(9)]
    return ident, points


def main():
    with UR44() as d:
        _, groups, before, _ = decode(d.request_dump(3))
        morph_groups = [g for g in groups if g[1][0][0] in (0x51, 0x53)]
        values = {k: [list(x) if isinstance(x, list) else x for x in v] for k, v in before.items()}

        if sys.argv[1] == "load":
            strip = int(sys.argv[2]) - 1
            ident, points = read_ssd(sys.argv[3])
            name = os.path.splitext(os.path.basename(sys.argv[3]))[0].encode("latin1")
            values[0x51][strip] = list(ident.ljust(4, b"\0"))
            values[0x52][strip] = list(name.ljust(256, b"\0"))
            for p in range(9):
                for i, pid in enumerate(MORPH_IDS):
                    values[pid][strip * 9 + p] = points[p][i]
            print(f"loading {name.decode()} (id {ident.decode()}) into strip {strip + 1}")

        payload = encode("MorphingTable", morph_groups, values)
        msg = bulk_message(2, payload)
        print(f"sending {len(msg)} bytes ({len(payload)} payload)")
        d.send(msg)
        end = time.monotonic() + 1.0
        while (left := end - time.monotonic()) > 0:
            m = d.recv(left)
            if m:
                print("  reply:", m[:20].hex(" ").upper(), "..." if len(m) > 20 else "")
        _, _, after, _ = decode(d.request_dump(3))
        changed = {k for k in before if before[k] != after.get(k)}
        print("changed parameter ids:", sorted(hex(k) for k in changed) or "none")
        if sys.argv[1] == "load":
            ok = all(after[pid][strip * 9 + p] == points[p][i] for p in range(9) for i, pid in enumerate(MORPH_IDS))
            print("morph table now matches the preset:", ok,
                  "| name:", bytes(after[0x52][strip]).split(b"\0")[0].decode(), "| id:", bytes(after[0x51][strip]).decode())


if __name__ == "__main__":
    main()
