#!/usr/bin/env python3
"""Live probe: the store command, F0 43 30 3E 14 03 02 01 <type:u16> F7 (MemAPI PJ_COMMAND 1 / 6).

  probe_store.py TYPE     send it (1 = scene, 2 = morph table) and print the device's replies

ParameterChange::convertToSysEx maps command 1 to convertStore(1) and 6 to convertStore(2), next to the dump requests
of types 1 and 2; MemAPI::SetSceneData sends command 1 right after a scene bulk dump when told to "send and store".
"""
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools"))
from ur44 import UR44, u16  # noqa: E402


def main():
    kind = int(sys.argv[1])
    msg = bytes([0xF0, 0x43, 0x30, 0x3E, 0x14, 0x03, 0x02, 0x01, *u16(kind), 0xF7])
    with UR44() as d:
        print("send:", msg.hex(" ").upper())
        t0 = time.monotonic()
        d.send(msg)
        end = t0 + 3.0
        while (left := end - time.monotonic()) > 0:
            m = d.recv(left)
            if m:
                print(f"  +{time.monotonic() - t0:.3f}s reply:", m[:24].hex(" ").upper(), "..." if len(m) > 24 else "")


if __name__ == "__main__":
    main()
