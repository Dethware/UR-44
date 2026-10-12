#!/usr/bin/env bash
# Decompile every function of the interesting vendor binaries with Ghidra headless.
# Output: vendor/analysis/ghidra/<name>.c (one big file per binary, "// ==== FUN_x @ addr" separators).
# Needs Ghidra 12.x and a JDK 21 (GHIDRA_INSTALL_DIR / JAVA_HOME may be overridden).
set -euo pipefail
cd "$(dirname "$0")/../.."
GHIDRA_INSTALL_DIR=${GHIDRA_INSTALL_DIR:-/opt/ghidra}
export JAVA_HOME=${JAVA_HOME:-/usr/lib/jvm/java-21-openjdk}
E=vendor/extracted
OUT=vendor/analysis/ghidra
PROJ=$(mktemp -d)
trap 'rm -rf "$PROJ"' EXIT
mkdir -p "$OUT"

run() {
  "$GHIDRA_INSTALL_DIR/support/analyzeHeadless" "$PROJ" "$1" -import "$2" -overwrite \
    -scriptPath re/ghidra -postScript DumpAll.java "$PWD/$OUT/$1.c" > "$PROJ/$1.log" 2>&1
  echo "$1: $(grep -c '^// ====' "$OUT/$1.c") functions"
}

run memapi   "$E/tools_v222/dspmixfx_x86/memapi_ur44.dll" &
run bulklib  "$E/tools_v222/ysusb_driver/x86/ysusb_bulklib32.dll" &
run aadm     "$E/tools_v222/dspmixfx_x86/ysusb_aadm_extension.dll" &
run ext      "$E/tools_v222/dspmixfx_x86/ur44_extension.dll" &
wait
# the big ones (several minutes each)
run dspmixfx "$E/tools_v222/dspmixfx_x86/dspmixfx_ur44.exe" &
run fwupd    vendor/downloads/FirmwareUpdater_V212/UR44_firmware_V212.exe &
wait
