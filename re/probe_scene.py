#!/usr/bin/env python3
"""Live probe: send a scene to the UR44 (bulk dump type 1, "CurrentScene"), as dspMixFx does on scene recall.

  probe_scene.py noop       re-send the device's current scene (must change nothing)
  probe_scene.py tweak      send it with IN 6's MIX 2 pan moved, then send the original back
  probe_scene.py insert     send it with Channel Strip 4 inserted on IN 6 (MON), then send the original back

Every mode prints what changed (from "All" dumps before and after) and ends with the original scene restored.
The original is also written to $TMPDIR/ur44_scene_backup.bin first.
"""
import os
import sys
import tempfile
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools"))
from ur44 import UR44  # noqa: E402
from ur44_dump import decode, encode  # noqa: E402
from probe_morph_table import bulk_message  # noqa: E402
from ur44_params import PARAMS  # noqa: E402

NAMES = {r[0]: r[1] for r in PARAMS}
ID = {r[1]: r[0] for r in PARAMS}


def send_scene(d, payload):
    d.send(bulk_message(1, payload))
    end = time.monotonic() + 1.0
    replies = []
    while (left := end - time.monotonic()) > 0:
        m = d.recv(left)
        if m:
            replies.append(m[:16].hex(" ").upper())
    time.sleep(0.2)
    return replies


def changes(before, after):
    out = []
    for k in sorted(set(before) | set(after)):
        if before.get(k) != after.get(k):
            b, a = before.get(k), after.get(k)
            diff = [(i, x, y) for i, (x, y) in enumerate(zip(b or [], a or [])) if x != y]
            out.append(f"{NAMES.get(k, hex(k))}: " + ", ".join(f"[{i}] {x}->{y}" for i, x, y in diff[:6]))
    return out or ["nothing"]


def main():
    mode = sys.argv[1]
    with UR44() as d:
        original = d.request_dump(1)
        open(os.path.join(tempfile.gettempdir(), "ur44_scene_backup.bin"), "wb").write(original)
        name, groups, values, _ = decode(original)
        before = decode(d.request_dump(3))[2]

        payload = original
        if mode in ("tweak", "insert"):
            values = {k: [list(x) if isinstance(x, list) else x for x in v] for k, v in values.items()}
            if mode == "tweak":
                values[ID["ChSendMixBus2Pan"]][5] = 5 if values[ID["ChSendMixBus2Pan"]][5] != 5 else -5
            else:
                values[ID["ChFxInsPostPatch"]][5] = 4  # CHSTRIP 4
            payload = encode(name, groups, values)
            print("re-encoded identical to the original apart from the change:", len(payload) == len(original))

        print("send:", send_scene(d, payload))
        during = decode(d.request_dump(3))[2]
        print("changed:", *changes(before, during), sep="\n  ")

        if payload is not original:
            print("restore:", send_scene(d, original))
            after = decode(d.request_dump(3))[2]
            print("after restoring, differences from the start:", *changes(before, after), sep="\n  ")


if __name__ == "__main__":
    main()
