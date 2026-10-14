#!/usr/bin/env python3
"""Extract the dspMixFx scene embedded in a factory Cubase template (TOOLS for UR44 "steinberg_ur44_*" files).

The templates are Cubase AI 7 projects (RIFF "NUNDROOT"). The UR44 extension stores the mixer state in them, after a
"MemAPI" tag, as MemAPI::GetSceneData output: the same F_CurrentScene/D_CurrentScene chunk pair as a bulk dump
payload, except that the 32-byte chunk headers are little-endian (host order). This rewrites the headers to the
device's big-endian form, so tools/ur44_dump.py (and the GUI's BulkDump) decode the result.

Usage: extract_template_scenes.py OUT_DIR TEMPLATE...    (writes OUT_DIR/<template>.scene)
"""
import os
import struct
import sys


def extract(raw):
    f = raw.find(b"F_CurrentScene")
    if f < 0:
        raise ValueError("no F_CurrentScene chunk")
    fsize = struct.unpack_from("<I", raw, f + 28)[0]
    d = f + 32 + fsize
    if raw[d:d + 14] != b"D_CurrentScene":
        raise ValueError("D_CurrentScene doesn't follow F_CurrentScene")
    dsize = struct.unpack_from("<I", raw, d + 28)[0]

    def device_chunk(off, size):
        header = raw[off:off + 16] + bytes.fromhex("0100000000000000000027740000") + struct.pack(">H", size)
        return header + raw[off + 32:off + 32 + size]

    return device_chunk(f, fsize) + device_chunk(d, dsize)


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    for path in sys.argv[2:]:
        payload = extract(open(path, "rb").read())
        target = os.path.join(out, os.path.basename(path) + ".scene")
        open(target, "wb").write(payload)
        print(f"{path}: {len(payload)} bytes -> {target}")


if __name__ == "__main__":
    main()
