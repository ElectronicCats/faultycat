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

RED='\033[0;31m'
GREEN='\033[0;32m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'

info()    { printf "${CYAN}==>${NC} %s\n" "$*"; }
success() { printf "${GREEN}✓${NC} %s\n" "$*"; }
error()   { printf "${RED}${BOLD}ERROR:${NC} %s\n" "$*" >&2; }

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
default_uf2="$repo_root/build/fw-debug/apps/faultycat_fw/faultycat.uf2"
uf2="${1:-$default_uf2}"

if [ ! -f "$uf2" ]; then
    error "UF2 not found: $uf2"
    error "Build it first:   cmake --preset fw-debug && cmake --build build/fw-debug"
    exit 1
fi

# If the board is running the v3 firmware (1209:fa17) but isn't in
# BOOTSEL yet, kick it via the magic-baud path first. If it is
# already in BOOTSEL, skip (bootsel.sh would no-op anyway).
if lsusb 2>/dev/null | grep -qi "1209:fa17"; then
    info "board running — kicking into BOOTSEL via magic baud"
    "$(dirname "$0")/bootsel.sh" || true
fi

# Poll for the RPI-RP2 mount or the raw USB device (for picotool) with a
# timeout. After a magic-baud kick the board takes a few seconds to
# re-enumerate and for the desktop automounter to create the mount point.
rpi_mount=""
wait_secs=15
for i in $(seq 1 "$wait_secs"); do
    for candidate in \
        "/media/${USER}/RPI-RP2" \
        "/run/media/${USER}/RPI-RP2" \
        "/Volumes/RPI-RP2"
    do
        if [ -d "$candidate" ]; then
            rpi_mount="$candidate"
            break 2
        fi
    done
    # Also stop early if picotool can already see the device.
    if command -v picotool >/dev/null 2>&1 && \
       lsusb 2>/dev/null | grep -qi "2e8a:0003"; then
        break
    fi
    [ "$i" -eq 1 ] && info "waiting for RPI-RP2 to enumerate (up to ${wait_secs}s)..."
    sleep 1
done

if command -v picotool >/dev/null 2>&1; then
    info "picotool load $uf2"
    picotool load -xf "$uf2"
    exit 0
fi

if [ -n "$rpi_mount" ]; then
    info "cp $uf2 $rpi_mount/"
    cp "$uf2" "$rpi_mount/"
    sync
    success "flashed (drag-drop). Board will re-enumerate."
    exit 0
fi

error "no picotool installed and no RPI-RP2 mount detected."
error "Hold BOOTSEL while plugging in the FaultyCat, then re-run."
error "Or install picotool:  https://github.com/raspberrypi/picotool"
exit 1
