#!/usr/bin/env bash
# Build a curated projectM preset directory that excludes the MilkDrop2
# pixel-shader presets (those with warp_1=/comp_1= shader blocks). Those presets'
# HLSL shaders fail to compile on the Raspberry Pi's GLES stack, so projectM 3.1
# renders them as a solid white frame. The remaining classic (non-shader) presets
# render reliably.
#
# Output goes in the 240-MP data dir, which survives app rebuilds. The Bluetooth
# module's BluetoothBackend::preset_dir() prefers this dir and falls back to the
# full system set if it's missing.
#
# Usage: scripts/build-projectm-presets.sh [SRC_DIR] [DST_DIR]
set -euo pipefail

SRC="${1:-/usr/share/projectM/presets}"
DST="${2:-${XDG_DATA_HOME:-$HOME/.local/share}/240-MP/projectm-presets}"

if [ ! -d "$SRC" ]; then
    echo "source preset dir not found: $SRC (install the 'projectm-data' package)" >&2
    exit 1
fi

rm -rf "$DST"
mkdir -p "$DST"

# Copy every .milk preserving the subfolder layout (projectM scans recursively).
rsync -a --include='*/' --include='*.milk' --exclude='*' "$SRC/" "$DST/"

# Drop the pixel-shader presets, then prune empty folders.
grep -rlE '^(warp|comp)_1=' "$DST" --include=*.milk 2>/dev/null | tr '\n' '\0' | xargs -0 -r rm -f
find "$DST" -type d -empty -delete

echo "projectM clean presets: $(find "$DST" -iname '*.milk' | wc -l) in $DST"
