#!/usr/bin/env python3
"""Generate gui/src/ur44/parameters.json from tools/ur44_params.py and tools/ur44_labels.json.

One entry per parameter id:
  {"id", "name", "min", "max", "default", "instances", "unit", "labels"}
labels[0] is the label of min; null means "plain number".

Usage: gen_gui_params.py
"""
import json
import os
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
sys.path.insert(0, os.path.join(ROOT, "tools"))
from ur44_params import ALIASES, PARAMS  # noqa: E402

labels = json.load(open(os.path.join(ROOT, "tools", "ur44_labels.json")))
out = []
for pid, name, group, lo, hi, default, instances in PARAMS:
    lab = labels[str(pid)]
    assert lab["min"] == lo and lab["max"] == hi, name
    out.append({"id": pid, "name": name, "group": group, "min": lo, "max": hi, "default": default,
                "instances": instances, "unit": lab["unit"], "labels": lab["labels"]})

doc = {"parameters": out, "aliases": ALIASES}
path = os.path.join(ROOT, "gui", "src", "ur44", "parameters.json")
os.makedirs(os.path.dirname(path), exist_ok=True)
with open(path, "w") as f:
    json.dump(doc, f, indent=1)
    f.write("\n")
print(f"{len(out)} parameters -> {os.path.relpath(path, ROOT)}")
