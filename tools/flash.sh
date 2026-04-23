#!/usr/bin/env bash
# tools/flash.sh — flash a UF2 to the FaultyCat / RP2040 in BOOTSEL mode.
#
# Usage:
#   tools/flash.sh                              # flashes fw-debug UF2
#   tools/flash.sh build/fw-release/apps/...    # flashes a specific UF2
#
# Prefers `picotool` (fast, no mount required). Falls back to copying the
# UF2 to a mounted RPI-RP2 drive.

set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
default_uf2="$repo_root/build/fw-debug/apps/faultycat_fw/faultycat.uf2"
uf2="${1:-$default_uf2}"

if [ ! -f "$uf2" ]; then
    echo "ERROR: UF2 not found: $uf2" >&2
    echo "       Build it first:   cmake --preset fw-debug && cmake --build build/fw-debug" >&2
    exit 1
fi

if command -v picotool >/dev/null 2>&1; then
    echo "==> picotool load $uf2"
    picotool load -xf "$uf2"
    exit 0
fi

# Fallback: find a mounted RPI-RP2 drive.
for candidate in \
    "/media/${USER}/RPI-RP2" \
    "/run/media/${USER}/RPI-RP2" \
    "/Volumes/RPI-RP2"
do
    if [ -d "$candidate" ]; then
        echo "==> cp $uf2 $candidate/"
        cp "$uf2" "$candidate/"
        sync
        echo "✓ flashed (drag-drop). Board will re-enumerate."
        exit 0
    fi
done

cat <<'EOF' >&2
ERROR: no picotool installed and no RPI-RP2 mount detected.
  Hold BOOTSEL while plugging in the FaultyCat, then re-run.
  Or install picotool:  https://github.com/raspberrypi/picotool
EOF
exit 1
