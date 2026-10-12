#!/usr/bin/env python3
"""Extract the firmware image embedded as a Qt resource in UR44_firmware_V212.exe.

The updater is statically linked Qt with an rcc v1 resource bundle:
  names:  u16 len, u32 hash, UTF-16BE name
  tree:   14-byte entries (u32 name_off, u16 flags, dir: u32 count, u32 first | file: u16 country, u16 lang, u32 data_off)
  data:   u32 size + payload; flags & 1 => payload = u32 uncompressed size + zlib stream
Usage: extract_qrc_firmware.py UR44_firmware_V212.exe OUTDIR
"""
import os
import struct
import sys
import zlib

exe, outdir = sys.argv[1], sys.argv[2]
d = open(exe, "rb").read()

name = "upd_ur44_boot_main_v212.bin"
name_at = d.find(name.encode("utf-16-be")) - 6
# names table starts at the first of the consecutive entries ending at name_at; here icon.png precedes it
names = d.rfind("icon.png".encode("utf-16-be"), 0, name_at) - 6
tree = name_at + 6 + 2 * len(name)
while d[tree:tree + 4] != b"\0\0\0\0" or struct.unpack_from(">H", d, tree + 4)[0] != 2:
    tree += 1  # skip alignment padding up to the root directory entry


def entry(i):
    o = tree + 14 * i
    n_off, flags = struct.unpack_from(">IH", d, o)
    ln = struct.unpack_from(">H", d, names + n_off)[0]
    nm = d[names + n_off + 6:names + n_off + 6 + 2 * ln].decode("utf-16-be")
    if flags & 2:
        return nm, flags, struct.unpack_from(">II", d, o + 6)
    return nm, flags, struct.unpack_from(">HHI", d, o + 6)


_, _, (count, first) = entry(0)
files = {nm: (flags, rest[2]) for nm, flags, rest in (entry(first + k) for k in range(count))}

# data base: the PNG at data offset 0 is preceded by its u32 length
png = d.find(b"\x89PNG\r\n\x1a\n")
while png >= 0:
    base = png - 4
    fl, off = files[name]
    sz = struct.unpack_from(">I", d, base + off)[0]
    if 0 < sz < len(d) and d[base + off + 8:base + off + 9] == b"\x78":
        break
    png = d.find(b"\x89PNG\r\n\x1a\n", png + 1)
else:
    sys.exit("data section not found")

os.makedirs(outdir, exist_ok=True)
for nm, (fl, off) in files.items():
    sz = struct.unpack_from(">I", d, base + off)[0]
    blob = d[base + off + 4:base + off + 4 + sz]
    if fl & 1:
        blob = zlib.decompress(blob[4:])
    with open(os.path.join(outdir, nm), "wb") as f:
        f.write(blob)
    print(f"{nm}: {len(blob)} bytes (data @0x{base + off:x}, compressed={bool(fl & 1)})")
