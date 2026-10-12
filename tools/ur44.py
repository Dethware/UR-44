#!/usr/bin/env python3
"""Minimal UR44 SysEx client over ALSA rawmidi (ctypes, no deps).

Protocol (recovered from memapi_ur44.dll, see docs/PROTOCOL.md):
  header   F0 43 <type|dev> 3E 14
    type   0x10 = parameter change / reply, 0x20 = dump request, 0x30 = parameter request
  get      F0 43 30 3E 14 01 01 <param:3x7> <index:3x7> F7
  set      F0 43 10 3E 14 01 01 <param:3x7> <index:3x7> <value:5x7> F7
  version  F0 43 30 3E 14 00 70 02 F7

The firmware does not bounds-check the index: out-of-range reads return whatever
memory is there, so get()/set() refuse indices beyond the known instance count.
"""
import argparse
import ctypes
import ctypes.util
import sys
import time

import json
import os

from ur44_params import BY_ID, BY_NAME

# Display labels extracted from memapi (tools/dump_memapi_labels.py); labels[0] is the label of `min`.
with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "ur44_labels.json")) as _f:
    LABELS = {int(k): v for k, v in json.load(_f).items()}

PORT = "hw:0,0,1"  # control lives on the 2nd USB-MIDI port
REPLY_LEN = 19

_lib = ctypes.CDLL(ctypes.util.find_library("asound"))
SND_RAWMIDI_NONBLOCK = 2


def u16(v):
    return [(v >> 14) & 0x03, (v >> 7) & 0x7F, v & 0x7F]


def u32(v):
    v &= 0xFFFFFFFF
    return [(v >> 28) & 0x0F, (v >> 21) & 0x7F, (v >> 14) & 0x7F, (v >> 7) & 0x7F, v & 0x7F]


def dec(b):
    v = 0
    for x in b:
        v = (v << 7) | x
    return v


def signed32(v):
    return v - (1 << 32) if v & 0x80000000 else v


def parse_param_msg(msg):
    """Decode a 0x1n parameter message into (param, index, value) or None."""
    if (len(msg) != REPLY_LEN or msg[1] != 0x43 or msg[2] & 0xF0 != 0x10
            or msg[3:5] != b"\x3e\x14" or msg[5] != 0x01):
        return None
    return dec(msg[7:10]), dec(msg[10:13]), dec(msg[13:18])


def resolve(param):
    """Accept a numeric id or a parameter name; return the table row."""
    if isinstance(param, str):
        if param in BY_NAME:
            return BY_NAME[param]
        param = int(param, 0)
    if param not in BY_ID:
        raise KeyError(f"unknown parameter {param!r}")
    return BY_ID[param]


def label(param, value):
    """Human-readable text for a raw value, as dspMixFx shows it."""
    row = resolve(param)
    info = LABELS[row[0]]
    if value is None:
        return "?"
    if info["labels"] and row[3] <= value <= row[4]:
        text = info["labels"][value - row[3]]
    else:
        text = str(value)
    return f"{text} {info['unit']}".strip()


def check_index(row, index):
    if not 0 <= index < row[6]:
        raise IndexError(f"{row[1]} has {row[6]} instance(s); index {index} out of range")


class UR44:
    def __init__(self, port=PORT):
        self.i = ctypes.c_void_p()
        self.o = ctypes.c_void_p()
        r = _lib.snd_rawmidi_open(ctypes.byref(self.i), ctypes.byref(self.o),
                                  port.encode(), SND_RAWMIDI_NONBLOCK)
        if r < 0:
            raise OSError(-r, f"snd_rawmidi_open({port}) failed")
        self.buf = bytearray()
        self.unsolicited = []  # messages received while waiting for something else
        self.flush()

    def flush(self):
        while self.recv(0.05) is not None:
            pass
        self.unsolicited.clear()

    def send(self, msg):
        data = bytes(msg)
        _lib.snd_rawmidi_write(self.o, data, len(data))
        _lib.snd_rawmidi_drain(self.o)

    def recv(self, timeout=0.3):
        """Return the next complete SysEx message, or None on timeout."""
        end = time.monotonic() + timeout
        tmp = ctypes.create_string_buffer(1024)
        while True:
            s = self.buf.find(0xF0)
            if s >= 0:
                e = self.buf.find(0xF7, s)
                if e >= 0:
                    msg = bytes(self.buf[s:e + 1])
                    del self.buf[:e + 1]
                    return msg
            n = _lib.snd_rawmidi_read(self.i, tmp, 1024)
            if n > 0:
                self.buf += tmp.raw[:n]
                continue
            if time.monotonic() > end:
                return None
            time.sleep(0.002)

    def wait_for(self, match, timeout=0.3):
        """Read until match(msg) is true; stash everything else in self.unsolicited."""
        end = time.monotonic() + timeout
        while True:
            left = end - time.monotonic()
            if left <= 0:
                return None
            m = self.recv(left)
            if m is None:
                return None
            if match(m):
                return m
            self.unsolicited.append(m)

    def version(self):
        self.send([0xF0, 0x43, 0x30, 0x3E, 0x14, 0x00, 0x70, 0x02, 0xF7])
        r = self.wait_for(lambda m: m[5:8] == b"\x00\x70\x02")
        return r[8:-1].replace(b"\0", b" ").decode("ascii", "replace").split() if r else None

    def get_raw(self, param, index=0, timeout=0.3, retries=1):
        match = lambda m: (p := parse_param_msg(m)) is not None and p[:2] == (param, index)
        for _ in range(retries + 1):
            self.send([0xF0, 0x43, 0x30, 0x3E, 0x14, 0x01, 0x01, *u16(param), *u16(index), 0xF7])
            r = self.wait_for(match, timeout)
            if r:
                return parse_param_msg(r)[2]
        return None

    def set_raw(self, param, index, value):
        self.send([0xF0, 0x43, 0x10, 0x3E, 0x14, 0x01, 0x01, *u16(param), *u16(index), *u32(value), 0xF7])

    def get(self, param, index=0):
        row = resolve(param)
        check_index(row, index)
        v = self.get_raw(row[0], index)
        if v is None:
            return None
        return signed32(v) if row[3] < 0 else v

    def set(self, param, index, value):
        row = resolve(param)
        check_index(row, index)
        if not row[3] <= value <= row[4]:
            raise ValueError(f"{row[1]} range is {row[3]}..{row[4]}, got {value}")
        self.set_raw(row[0], index, value)


def main():
    num = lambda x: int(x, 0)
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default=PORT)
    sp = ap.add_subparsers(dest="cmd", required=True)
    sp.add_parser("version")
    g = sp.add_parser("get", help="read a parameter (name or id)")
    g.add_argument("param"); g.add_argument("index", type=num, nargs="?", default=0)
    s = sp.add_parser("set", help="write a parameter (name or id), range-checked")
    s.add_argument("param"); s.add_argument("index", type=num); s.add_argument("value", type=num)
    sp.add_parser("dump", help="read every known parameter instance")
    mx = sp.add_parser("mix", help="show the routing of MIX 1 or MIX 2")
    mx.add_argument("bus", type=int, choices=[1, 2])
    ph = sp.add_parser("phones2", help="show or select the PHONES 2 source")
    ph.add_argument("bus", type=int, choices=[1, 2], nargs="?")
    sp.add_parser("monitor", help="print everything the device sends")
    a = ap.parse_args()

    d = UR44(a.port)
    if a.cmd == "version":
        print(d.version())
    elif a.cmd == "get":
        print(d.get(a.param, a.index))
    elif a.cmd == "set":
        d.set(a.param, a.index, a.value)
        print(d.get(a.param, a.index))
        for m in d.unsolicited:
            print("unsolicited:", m.hex(" ").upper())
    elif a.cmd == "dump":
        for pid in sorted(BY_ID):
            row = BY_ID[pid]
            vals = [d.get(pid, i) for i in range(row[6])]
            print(f"0x{pid:02x} {row[1]:28s} " + " | ".join(label(pid, v) for v in vals))
    elif a.cmd == "mix":
        b = a.bus
        g = lambda name, i=0: label(name, d.get(name, i))
        print(f"MIX {b}")
        for ch in range(6):
            print(f"  IN {ch + 1}    {g(f'ChSendMixBus{b}Onf', ch):3s}  {g(f'ChSendMixBus{b}Level', ch):>10s}  "
                  f"pan {g(f'ChSendMixBus{b}Pan', ch):>3s}  solo {g(f'ChSendMixBus{b}SoloOnf', ch)}")
        print(f"  DAW     {g(f'DawSendMixBus{b}Onf'):3s}  {g(f'DawSendMixBus{b}Level'):>10s}  "
              f"bal {g(f'DawSendMixBus{b}Bal'):>3s}  solo {g(f'DawSendMixBus{b}SoloOnf')}")
        print(f"  REV-X   {g(f'FxRevMixBus{b}Onf'):3s}  {g(f'FxRevBus{b}Level'):>10s}  (return)")
        print(f"  MASTER  {g(f'MasterMixBus{b}Onf'):3s}  {g(f'MasterMixBus{b}Level'):>10s}  "
              f"bal {g(f'MasterMixBus{b}Bal'):>3s}")
        print(f"  PHONES 2 source: {g('PatchAuxPhoneOutput')}")
    elif a.cmd == "phones2":
        if a.bus:
            d.set("PatchAuxPhoneOutput", 0, a.bus - 1)
        print("PHONES 2 source:", label("PatchAuxPhoneOutput", d.get("PatchAuxPhoneOutput")))
    elif a.cmd == "monitor":
        while True:
            m = d.recv(3600)
            if m:
                p = parse_param_msg(m)
                if p and p[0] in BY_ID:
                    print(f"{m.hex(' ').upper()}   {BY_ID[p[0]][1]}[{p[1]}] = {p[2]}", flush=True)
                else:
                    print(m.hex(" ").upper(), flush=True)


if __name__ == "__main__":
    sys.exit(main())
