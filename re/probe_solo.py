#!/usr/bin/env python3
"""Live probe: how the UR44 handles (multiple) solos, measured on the MIX 2 master meter.

macOS MemAPI (smd_SetPrmRelSolo) keeps SysSoloOnf = OR of all channel/DAW solos of both mixes, which suggests
additive solo (several soloed channels audible, the rest muted). This checks whether the device derives
SysSoloOnf itself and what the mix sounds like in each state. Only MIX 2 is touched (LINE 3/4), briefly.
Everything changed is restored.

Usage: probe_solo.py
"""
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools"))
from ur44 import UR44, dec, meter_db  # noqa: E402


def master_level(d, seconds=1.0):
    """Peak of the MIX 2 master meter (L) over `seconds`, in dBFS."""
    d.meters(True)
    peak, end = 0, time.monotonic() + seconds
    while time.monotonic() < end:
        m = d.recv(0.3)
        if m and m[1:7] == b"\x43\x10\x3e\x14\x02\x02":
            body = m[7:-1]
            vals = [dec(body[i:i + 3]) for i in range(0, len(body) - 2, 3)]
            peak = max(peak, vals[16])
    d.meters(False)
    d.flush(0.2)
    return meter_db(peak)


def main():
    d = UR44()
    names = ["ChSendMixBus2SoloOnf", "DawSendMixBus2SoloOnf", "ChSendMixBus1SoloOnf", "SysSoloOnf",
             "PatchMasterMeter"]
    saved = {}
    for n in names:
        count = 6 if n.startswith("Ch") else 1
        for i in range(count):
            saved[(n, i)] = d.get(n, i)
    print("before:", {f"{n}[{i}]": v for (n, i), v in saved.items() if v})

    def state(tag):
        print(f"{tag:42s} SysSoloOnf={d.get('SysSoloOnf')}  MIX2 master peak {master_level(d)} dBFS")

    try:
        d.set("PatchMasterMeter", 0, 1)
        state("no solo")
        d.set("ChSendMixBus2SoloOnf", 5, 1)      # IN 6: nothing connected
        state("solo IN6 (silent)")
        d.set("ChSendMixBus2SoloOnf", 0, 1)      # IN 1: the microphone
        state("solo IN6 + IN1")
        d.set("ChSendMixBus2SoloOnf", 5, 0)
        state("solo IN1 only")
        d.set("ChSendMixBus2SoloOnf", 0, 0)
        if d.get("SysSoloOnf"):
            state("all solos off (SysSoloOnf still set)")
            d.set("SysSoloOnf", 0, 0)
        state("all solos off")

        # Does the host have to drive SysSoloOnf? Solo IN6 with SysSoloOnf forced off, then forced on
        d.set("ChSendMixBus2SoloOnf", 5, 1)
        d.set("SysSoloOnf", 0, 0)
        state("solo IN6, SysSoloOnf forced 0")
        d.set("SysSoloOnf", 0, 1)
        state("solo IN6, SysSoloOnf forced 1")
        d.set("ChSendMixBus2SoloOnf", 5, 0)
        state("no channel solo, SysSoloOnf forced 1")
    finally:
        for (n, i), v in saved.items():
            if v is not None:
                d.set(n, i, v)
        d.meters(False)
        after = {(n, i): d.get(n, i) for (n, i) in saved}
        print("restored" if after == saved else f"RESTORE MISMATCH: {after}")


if __name__ == "__main__":
    main()
