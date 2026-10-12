#!/usr/bin/env python3
"""Extract the UR44 firmware image from a firmware-updater executable (Windows .exe or macOS Mach-O).

Both updaters embed the image as a zlib-compressed Qt resource (":/upd_ur44_boot_main_v212.bin").
Rather than parse the rcc structures (the layout differs between Qt 4 on Windows and Qt 5/6 on
macOS), scan for zlib streams that inflate to the image magic "MMPFUPDD".

For reference, Windows UR44_firmware_V212.exe uses rcc v1: names table at file 0x4946a8
(u16 len, u32 hash, UTF-16BE name), 14-byte tree entries after it, and the firmware entry flagged
zlib (u32 size, u32 uncompressed size, stream) at file 0x44ed05.

Usage: extract_firmware.py UPDATER OUTDIR
"""
import hashlib
import os
import re
import sys
import zlib

MAGIC = b"MMPFUPDD"
NAME = "upd_ur44_boot_main_v212.bin"
KNOWN = {"8a721750aeee6c01b4f29fd445402ab8c357cc7d87908775b725a4f92d082cd4": "V2.12 (BOOT+MAIN)"}

src, outdir = sys.argv[1], sys.argv[2]
d = open(src, "rb").read()

found = 0
for m in re.finditer(b"\x78[\x01\x5e\x9c\xda]", d):
    try:
        z = zlib.decompressobj()
        if not z.decompress(d[m.start():m.start() + 4096], len(MAGIC)).startswith(MAGIC):
            continue
        image = zlib.decompress(d[m.start():], zlib.MAX_WBITS, 1 << 21)
    except zlib.error:
        continue
    digest = hashlib.sha256(image).hexdigest()
    os.makedirs(outdir, exist_ok=True)
    with open(os.path.join(outdir, NAME), "wb") as f:
        f.write(image)
    print(f"{NAME}: {len(image)} bytes from zlib stream at 0x{m.start():x}, "
          f"sha256 {digest} ({KNOWN.get(digest, 'UNKNOWN image')})")
    found += 1
    break

sys.exit(0 if found else "no firmware image found")
