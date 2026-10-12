"""Decode UR44 bulk dump payloads (after 7-bit unpacking).

Layout, observed in dumps of "CurrentScene" (request 1) and "All" (request 3):
  chunk  = 16-byte name ("F_<name>" / "D_<name>"), u16 BE 1, 10 zero bytes, u16 BE 0x2774, u32 BE?
           where the last u16 is the body length
  F_     = format: groups of  80 01 <instances:u16 BE>  followed by items
           <param id:u16 BE> <flags:u8> <size:u8>   flags 01 unsigned, 00 signed, 40 array
           (an array item is followed by its element count as u16 BE)
  D_     = values in the same order, item-major: for each item, the value of every instance
           (size bytes each, big-endian; signed when flags == 00)
"""
import struct

HEADER = 32


def _chunks(raw):
    off = 0
    while off + HEADER <= len(raw):
        name = raw[off:off + 16].split(b"\0")[0].decode("latin1")
        size = struct.unpack_from(">H", raw, off + 30)[0]
        yield name, raw[off + HEADER:off + HEADER + size]
        off += HEADER + size


def parse_format(body):
    """Return [(instances, [(param_id, flags, size, count)])]."""
    groups, i = [], 0
    while i + 4 <= len(body):
        if body[i] == 0x80:
            groups.append((struct.unpack_from(">H", body, i + 2)[0], []))
            i += 4
            continue
        pid, flags, size = struct.unpack_from(">HBB", body, i)
        i += 4
        count = 1
        if flags & 0x40:
            count = struct.unpack_from(">H", body, i)[0]
            i += 2
        groups[-1][1].append((pid, flags, size, count))
    return groups


def decode(raw):
    """Decode a dump payload into {param_id: [value per instance]} (arrays: list of lists)."""
    chunks = dict(_chunks(raw))
    fmt_name = next(n for n in chunks if n.startswith("F_"))
    groups = parse_format(chunks[fmt_name])
    data = chunks["D_" + fmt_name[2:]]
    values, i = {}, 0
    for instances, items in groups:
        for pid, flags, size, count in items:
            per_inst = []
            for _ in range(instances):
                elems = []
                for _ in range(count):
                    v = int.from_bytes(data[i:i + size], "big", signed=(flags & 0x7F) == 0)
                    elems.append(v)
                    i += size
                per_inst.append(elems if flags & 0x40 else elems[0])
            values[pid] = per_inst
    return fmt_name[2:], groups, values, len(data) - i
