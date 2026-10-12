#!/usr/bin/env python3
"""Reduce Ghidra pseudocode of SysEx builders (macOS MemAPI) to their byte sequences.

The builders push bytes into a std::vector<uchar>; each push is inlined as a realloc branch
(`p[-1] = X`) plus a fast path (`*p = X; p = p + 1;`). This keeps only the fast-path stores and
the helper calls (set*Header, CmdTranslator::translate*, sendSysEx, ...), in source order.

Usage: summarize_builders.py memapi_mac.c 'ParameterChange::convert'   (prefix match on function name)
"""
import re
import sys

path, prefix = sys.argv[1], sys.argv[2]
src = open(path).read()
funcs = re.split(r"^// ==== ", src, flags=re.M)[1:]
store = re.compile(r"^\s+\*(\w+) = ([^;]+);\s*$")
call = re.compile(r"\b((?:\w+::)*(?:set\w*Header|translate\w+|sendSysEx|send\w*|_makeChunkHeader|calcCheckSum|convert\w+)\s*\()")
for f in funcs:
    name = f.split(" @ ")[0]
    if not name.startswith(prefix):
        continue
    out = []
    for line in f.splitlines()[1:]:
        if m := store.match(line):
            if "+" in m[2] and m[2].strip().startswith(m[1]):  # pointer bump, not a data store
                continue
            v = m[2].strip()
            v = {"-9": "0xf7"}.get(v, v)
            out.append(v)
        elif (m := call.search(line)) and "operator" not in line:
            out.append(re.sub(r"\s+", " ", line.strip()))
    print(f"== {name}")
    for x in out:
        print("   ", x)
