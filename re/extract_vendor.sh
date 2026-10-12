#!/usr/bin/env bash
# Rebuild vendor/extracted/ from the original Yamaha/Steinberg downloads in vendor/downloads/
# (fetch those first with re/download_vendor.py).
# Needs: innoextract (>= 1.9), cabextract, msitools is NOT needed (the cabs carry the payload).
set -euo pipefail
cd "$(dirname "$0")/.."
DL=vendor/downloads
OUT=vendor/extracted
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

rm -rf "$OUT"
mkdir -p "$OUT"

# --- TOOLS for UR44 V2.2.2 (Inno Setup 5.5.7) -> MSI cabinets ---
innoextract -s -d "$TMP/tools" "$DL/TOOLS_for_UR44_V222_Win/setup.exe"
T="$TMP/tools/tmp"
U=$(echo "$TMP"/tools/commonappdata/Yamaha_Uninstaller/*/)

cabextract -q -d "$TMP/urap_comm" "$T/URApComm.cab"
cabextract -q -d "$TMP/urap_64" "$T/URAp64.cab"
for c in YSUSBCom YSUSB32 YSUSB64; do cabextract -q -d "$TMP/$c" "$U/$c.cab"; done

# dspMixFx application (32-bit) + 32-bit helpers. MSI cab names lose their extension
# sometimes ("…_vocal_", "….dll1"); keep the ones we care about with proper names.
A="$OUT/tools_v222/dspmixfx_x86"
mkdir -p "$A/scenes"
for f in dspmixfx_ur44.exe memapi_ur44.dll ur44_extension.dll ysusb_aadm_extension.dll \
         qtcore4.dll qtgui4.dll qtnetwork4.dll qtxml4.dll; do
  cp "$TMP/urap_comm/$f" "$A/"
done
# factory scene/preset files (Cubase RIFF "NUNDROOT" containers)
for f in "$TMP"/urap_comm/steinberg_ur44_*; do cp "$f" "$A/scenes/"; done

# 64-bit builds of the helper DLLs (the ".dll1" suffix is an MSI cab artifact)
B="$OUT/tools_v222/helpers_x64"
mkdir -p "$B"
cp "$TMP/urap_64/memapi_ur44.dll1" "$B/memapi_ur44.dll"
cp "$TMP/urap_64/ur44_extension.dll1" "$B/ur44_extension.dll"
cp "$TMP/urap_64/ysusb_aadm_extension.dll1" "$B/ysusb_aadm_extension.dll"

# Yamaha Steinberg USB driver bundled with TOOLS V2.2.2
D="$OUT/tools_v222/ysusb_driver"
mkdir -p "$D/x86" "$D/x64"
cp "$TMP/YSUSBCom/ysusb_bulklib32.dll" "$TMP/YSUSBCom/ysusb_asio32.dll" "$D/x86/"
cp "$TMP"/YSUSB32/*.sys "$TMP"/YSUSB32/*.inf "$TMP"/YSUSB32/*.cat "$TMP/YSUSB32/ysusb_cp.exe" "$D/x86/"
cp "$TMP"/YSUSB64/*.sys "$TMP"/YSUSB64/*.inf "$TMP"/YSUSB64/*.cat "$TMP"/YSUSB64/*.dll "$TMP/YSUSB64/ysusb_cp.exe" "$D/x64/"

# --- Firmware updater V2.12: firmware image is an embedded, zlib-compressed Qt resource ---
python3 -I re/extract_firmware.py "$DL/FirmwareUpdater_V212/UR44_firmware_V212.exe" "$OUT/firmware_v212"
# the macOS updater (optional "extra" download) embeds the identical image; extract it to cross-check
MACFW="$DL/FirmwareUpdater_V212-2_mac/UR44_firmware_V212.app/Contents/MacOS/UR44_firmware_V212"
if [ -f "$MACFW" ]; then
  python3 -I re/extract_firmware.py "$MACFW" "$TMP/macfw"
  cmp "$TMP/macfw/upd_ur44_boot_main_v212.bin" "$OUT/firmware_v212/upd_ur44_boot_main_v212.bin"
fi

# --- TOOLS for UR44 V2.3.0 for macOS (optional "extra" download): flat .pkg (xar) of gzip+cpio payloads ---
MACPKG="$DL/TOOLS_for_UR44_V230_Mac/TOOLS for UR44 V2.3.0.pkg"
if [ -f "$MACPKG" ]; then
  M="$OUT/tools_v230_mac"
  mkdir -p "$M"
  for p in dspMixFxUR44 UR44Extension ysusbAadmExtension; do
    7z x -y -o"$TMP/macpkg" "$MACPKG" "$p.pkg/Payload" > /dev/null
    mkdir -p "$M/$p"
    ( cd "$M/$p" && gzip -dc "$TMP/macpkg/$p.pkg/Payload" | cpio -idm --quiet )
  done
  # drop translated manuals bundled inside the app
  find "$M" -path '*.lproj/UR44_OperationManual.pdf' ! -path '*English.lproj*' -delete
fi

# --- YSUSB V2.1.7 standalone driver (optional "extra" download; Inno Setup 6.1) ---
if [ -f "$DL/YSUSB_V217_Win/setup.exe" ]; then
  innoextract -s -d "$TMP/ysusb217" "$DL/YSUSB_V217_Win/setup.exe"
  mkdir -p "$OUT/ysusb_v217"
  # the payload is again an MSI + sibling cab; the cab has the files
  for c in $(find "$TMP/ysusb217" -iname '*.cab'); do cabextract -q -d "$TMP/ysusb217_cab" "$c"; done
  cp "$TMP"/ysusb217_cab/* "$OUT/ysusb_v217/"
fi

# --- YSUSB V2.1.11 standalone driver: Inno Setup 6.7, not supported by innoextract 1.9 ---
echo "note: $DL/YSUSB_V2111_Win is Inno Setup 6.7; not extracted (needs newer innoextract)"

( cd "$OUT" && find . -type f | sort | xargs sha256sum ) > "$OUT/SHA256SUMS"
echo "extracted into $OUT"
