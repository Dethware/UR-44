#!/usr/bin/env bash
# Checks the UCM profile against the connected UR44 without installing anything: builds a copy of the system's UCM
# tree with these files on top, and prints what alsaucm makes of the card.
# Usage: linux/ucm2/test.sh [card]     (default: the card named UR44)
set -euo pipefail
here="$(dirname "$(readlink -f "$0")")"
card="${1:-$(awk '/USB-Audio - Steinberg UR44$/ {print $1; exit}' /proc/asound/cards)}"
[ -n "$card" ] || { echo "no UR44 card found" >&2; exit 1; }
tree=$(mktemp -d "${TMPDIR:-/tmp}/ur44-ucm.XXXXXX")
trap 'rm -rf "$tree"' EXIT
cp -r /usr/share/alsa/ucm2/. "$tree/"
cp -r "$here/USB-Audio/." "$tree/USB-Audio/"
echo "card $card: $(cat /proc/asound/card$card/usbid)"
ALSA_CONFIG_UCM2="$tree" alsaucm -c "hw:$card" dump text
echo "--- alsa-lib configuration (the channel splits)"
ALSA_CONFIG_UCM2="$tree" alsaucm -c "hw:$card" get _alibcfg 2>/dev/null || true
