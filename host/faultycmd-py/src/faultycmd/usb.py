"""Port → CDC interface mapping helper.

Linux primary: walks ``/dev/ttyACM*`` and uses ``udevadm info`` to
identify which interface number each ACM node maps to within the
FaultyCat v3 composite device (VID 0x1209 / PID 0xFA17).

Windows / macOS support is a TODO of F11 release polish; the helper
falls back to a heuristic that scans a list of candidate device
nodes if udevadm isn't available.

The composite layout (frozen in F3 — see
``docs/ARCHITECTURE.md::USB composite``):

==================  ====================  =======================
Interface (F3)      Role                  Default device on Linux
==================  ====================  =======================
IF 0 / 1            CDC0 — emfi           /dev/ttyACM<N>
IF 2 / 3            CDC1 — crowbar        /dev/ttyACM<N+1 or +2>
IF 4 / 5            CDC2 — scanner shell  /dev/ttyACM<N+...>
IF 6 / 7            CDC3 — target-uart    /dev/ttyACM<N+...>
IF 8                Vendor — CMSIS-DAP v2 (no /dev node)
IF 9                HID — CMSIS-DAP v1    (no /dev node)
==================  ====================  =======================

The trailing /dev/ttyACM<N> numbers are NOT stable across reboots
or when other USB-CDC devices share the namespace, so this module
always re-discovers via udevadm.
"""
from __future__ import annotations

import logging
import shutil
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Literal

log = logging.getLogger(__name__)

VID_FAULTYCAT = 0x1209
PID_FAULTYCAT = 0xFA17

#: Role → control-interface number. Data interfaces are control + 1.
INTERFACE_NUMBERS: dict[str, int] = {
    "emfi":     0x00,
    "crowbar":  0x02,
    "scanner":  0x04,
    "target":   0x06,
}

Role = Literal["emfi", "crowbar", "scanner", "target"]


@dataclass(frozen=True)
class FaultyCatPort:
    """One CDC interface of the FaultyCat composite."""

    interface: int
    device: str   # e.g. "/dev/ttyACM3"


class PortDiscoveryError(LookupError):
    """No FaultyCat CDC matched the requested role."""


def _udev_props(device: str) -> dict[str, str]:
    """Return the relevant udev properties for `device`, or {} on failure."""
    if not shutil.which("udevadm"):
        return {}
    try:
        out = subprocess.check_output(
            ["udevadm", "info", "--query=property", device],
            stderr=subprocess.DEVNULL,
            text=True,
        )
    except (subprocess.CalledProcessError, OSError):
        return {}
    props: dict[str, str] = {}
    for line in out.splitlines():
        if "=" not in line:
            continue
        key, _, value = line.partition("=")
        props[key.strip()] = value.strip()
    return props


def discover() -> list[FaultyCatPort]:
    """Walk ``/dev/ttyACM*`` and return ports owned by the FaultyCat
    composite, sorted by interface number.

    Returns an empty list on platforms without ``/dev/ttyACM*`` or
    without ``udevadm`` (Windows / macOS — TODO F11).
    """
    found: list[FaultyCatPort] = []
    for dev_path in sorted(Path("/dev").glob("ttyACM*")):
        props = _udev_props(str(dev_path))
        if not props:
            continue
        try:
            vid = int(props.get("ID_VENDOR_ID", "0"), 16)
            pid = int(props.get("ID_MODEL_ID", "0"), 16)
            iface_raw = props.get("ID_USB_INTERFACE_NUM")
        except ValueError:
            continue
        if vid != VID_FAULTYCAT or pid != PID_FAULTYCAT:
            continue
        if iface_raw is None:
            continue
        try:
            iface = int(iface_raw)
        except ValueError:
            continue
        found.append(FaultyCatPort(interface=iface, device=str(dev_path)))
    found.sort(key=lambda p: p.interface)
    return found


def cdc_for(role: Role) -> str:
    """Return the ``/dev/ttyACM<N>`` matching the requested CDC role.

    Raises:
        ValueError: ``role`` not in INTERFACE_NUMBERS.
        PortDiscoveryError: no FaultyCat CDC found at the expected
            interface (board not enumerated, or different VID/PID).
    """
    if role not in INTERFACE_NUMBERS:
        raise ValueError(
            f"unknown role {role!r}; expected one of {sorted(INTERFACE_NUMBERS)}"
        )
    target_iface = INTERFACE_NUMBERS[role]
    ports = discover()
    for port in ports:
        if port.interface == target_iface:
            return port.device
    raise PortDiscoveryError(
        f"no FaultyCat CDC found for role={role!r} "
        f"(expected interface 0x{target_iface:02X}; "
        f"discovered interfaces: {[hex(p.interface) for p in ports]})"
    )
