#!/usr/bin/env bash
# Print decompiled functions by (qualified) name from a DumpAll.java output file.
# Usage: re/show_func.sh vendor/analysis/ghidra/memapi_mac.c 'ParameterChange::convertStore' [...]
f=$1; shift
for name in "$@"; do
  awk -v n="$name" '/^\/\/ ==== /{p=($3==n)} p' "$f"
done
