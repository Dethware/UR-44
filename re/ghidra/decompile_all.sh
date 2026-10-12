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
# macOS MemAPI 2.3.0 keeps its symbols; decompile the x86_64 slice of the universal binary
MACMEM="$E/tools_v230_mac/dspMixFxUR44/dspMixFx_UR44.app/Contents/PlugIns/MemAPI_UR44.bundle/Contents/MacOS/MemAPI_UR44"
if [ -f "$MACMEM" ]; then
  llvm-lipo -thin x86_64 "$MACMEM" -output "$PROJ/MemAPI_UR44_x86_64"
  run memapi_mac "$PROJ/MemAPI_UR44_x86_64" &
  # FX editor plugins (symbols; their meterChanged() reveal the meter frame layout)
  for fx in ChannelStrip GA_Classics_CLEAN RevHall; do
    llvm-lipo -thin x86_64 "$(dirname "$MACMEM")/../../../fx/lib${fx}_UR44.dylib" -output "$PROJ/fx_$fx"
    run "mac_fx_$fx" "$PROJ/fx_$fx" &
  done
fi
wait
# the big ones (several minutes each)
run dspmixfx "$E/tools_v222/dspmixfx_x86/dspmixfx_ur44.exe" &
run fwupd    vendor/downloads/FirmwareUpdater_V212/UR44_firmware_V212.exe &
wait
