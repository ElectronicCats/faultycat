#!/usr/bin/env bash
# tools/bootsel.sh — trigger a running FaultyCat v3 to reboot into
# USB BOOTSEL mode without touching the physical button.
#
# How it works: the composite firmware watches every CDC's line
# coding. A host that sets the baud to 1200 is interpreted as
# "reboot into BOOTSEL" and the firmware jumps into the RP2040
# bootrom via reset_usb_boot(). After ~0.5 s the device re-enumerates
# as `RPI-RP2` mass storage.
#
# Usage:
#   tools/bootsel.sh                  # find first FaultyCat CDC
#   tools/bootsel.sh /dev/ttyACM3     # target a specific CDC

set -euo pipefail

TTY="${1:-}"

if [ -z "$TTY" ]; then
    shopt -s nullglob
    for d in /dev/ttyACM*; do
        if udevadm info -n "$d" 2>/dev/null | grep -q "ID_MODEL_ID=fa17"; then
            TTY="$d"
            break
        fi
    done
    shopt -u nullglob
fi

if [ -z "$TTY" ] || [ ! -e "$TTY" ]; then
    echo "ERROR: no FaultyCat CDC found under /dev/ttyACM*." >&2
    echo "       Pass the TTY explicitly, e.g. tools/bootsel.sh /dev/ttyACM3" >&2
    exit 1
fi

echo "==> triggering BOOTSEL via $TTY (magic baud 1200)"

# pyserial is more portable than stty for this — stty requires locking
# the port and some distros drop the setting. pyserial sets the line
# coding, waits a moment, and closes cleanly.
python3 - <<PY
import serial, time
try:
    s = serial.Serial("$TTY", 1200, dsrdtr=False)
    time.sleep(0.2)
    s.close()
except Exception as e:
    # Device re-enumerated while we held the port — that is the
    # success signal.
    pass
PY

echo "✓ waiting ~1 s for re-enumeration as RPI-RP2..."
sleep 1

for m in "/media/${USER}/RPI-RP2" "/run/media/${USER}/RPI-RP2" "/Volumes/RPI-RP2"; do
    if [ -d "$m" ]; then
        echo "✓ BOOTSEL mounted at $m"
        exit 0
    fi
done

echo "NOTE: BOOTSEL drive not auto-mounted yet."
echo "      Check \`lsusb | grep 2e8a:0003\` and your file manager."
