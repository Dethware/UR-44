#!/usr/bin/env bash
# Renders the application icon (org.dethware.ur44control.svg) to the PNGs built into the app (icons/), which serve
# as the tray and window icon where the installed theme icon isn't available. Run it after changing the SVG.
# The sizes must match the list in gui/src/application/CMakeLists.txt (qt_add_resources ... "icons").
# Needs rsvg-convert (librsvg).
set -euo pipefail
here="$(dirname "$(readlink -f "$0")")"
svg="$here/org.dethware.ur44control.svg"
out="$here/icons"
sizes=(16 22 24 32 48 64 128 256)

command -v rsvg-convert > /dev/null || { echo "rsvg-convert not found (it's in librsvg)" >&2; exit 1; }
mkdir -p "$out"
for size in "${sizes[@]}"; do
    rsvg-convert --width "$size" --height "$size" --output "$out/ur44control-$size.png" "$svg"
done
echo "rendered ${#sizes[@]} icons into $out"
