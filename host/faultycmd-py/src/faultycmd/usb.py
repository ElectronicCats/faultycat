"""Port → CDC interface mapping helper.

Cross-platform: uses ``pyserial.tools.list_ports`` so the same code
works on Linux (``/dev/ttyACM*``), Windows (``COM*``), and macOS
(``/dev/cu.usbmodem*``). Recovering the interface number inside the
composite needs a different trick per platform:

  - **Linux**: pyserial's ``location`` field ends in ``.<N>`` where
    ``N`` is the control-interface number.
  - **Windows**: pyserial's ``hwid`` carries an ``MI_XX`` token.
  - **macOS**: neither — pyserial's ``location`` is just the bus
    address (e.g. ``20-2``), and the hwid has no ``MI_XX``. Two
    fallbacks pick up the slack:
       1. ``port.interface`` (the iInterface string set by the
          firmware) matches one of ``"FaultyCat EMFI Control"``,
          ``"FaultyCat Crowbar Control"``, etc. when pyserial
          populates that field on macOS.
       2. The ``/dev/cu.usbmodem<...><N>`` device name itself
          encodes the **data**-interface number as the trailing
          digit — `usbmodem14201` → data IF 1 → control IF 0 (emfi),
          `usbmodem14203` → control IF 2 (crowbar), and so on.

On Linux ``udevadm`` is kept as a fallback for systems where
pyserial's location parsing misses the interface number (older
pyserial, weird kernel).

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

OS-stable port names (``/dev/ttyACMx`` numbering, ``COM<n>`` index)
shuffle across reboots / hot-plugs, so this module always
re-discovers by VID:PID and interface number rather than caching.
"""

from __future__ import annotations

import logging
import re
import shutil
import subprocess
from dataclasses import dataclass
from typing import Literal

from serial.tools import list_ports

log = logging.getLogger(__name__)

VID_FAULTYCAT = 0x1209
PID_FAULTYCAT = 0xFA17

#: Role → control-interface number. Data interfaces are control + 1.
INTERFACE_NUMBERS: dict[str, int] = {
    "emfi": 0x00,
    "crowbar": 0x02,
    "scanner": 0x04,
    "target": 0x06,
}

Role = Literal["emfi", "crowbar", "scanner", "target"]


@dataclass(frozen=True)
class FaultyCatPort:
    """One CDC interface of the FaultyCat composite."""

    interface: int
    device: str  # "/dev/ttyACMx" on Linux, "COMx" on Windows


class PortDiscoveryError(LookupError):
    """No FaultyCat CDC matched the requested role."""


# Windows hwid carries the interface number as "MI_XX" (hex).
_WIN_MI_RE = re.compile(r"MI_([0-9A-Fa-f]{2})")

# pyserial's `location` field carries the interface number as the
# last ".<n>" segment:
#   Linux:   "1-3:1.4"   → 4
#   Windows: "1-3:x.6"   → 6  (yes, literal 'x' — config index is
#                              unknown on Windows so pyserial fills it
#                              with that placeholder)
# Older pyserial on macOS used to emit a similar ".N" suffix but newer
# versions (and current macOS) just give the bus topology — e.g.
# `20-2` — without any interface marker. The macOS-specific fallbacks
# below pick up that case.
_LOCATION_IFACE_RE = re.compile(r"\.(\d+)\s*$")

# pyserial may surface the firmware's iInterface descriptor string in
# ``port.interface``. Our firmware sets one per CDC (see
# ``usb/src/usb_descriptors.c``), so a direct lookup recovers the
# role even when the location/hwid don't carry the interface number.
# Values are the CONTROL-interface number (matches INTERFACE_NUMBERS).
_INTERFACE_BY_STRING: dict[str, int] = {
    "FaultyCat EMFI Control": 0x00,
    "FaultyCat Crowbar Control": 0x02,
    "FaultyCat Scanner Shell": 0x04,
    "FaultyCat Target UART": 0x06,
}

# macOS `usbmodem` device names look like `/dev/cu.usbmodem<...><N>`
# where the trailing digit `N` is the DATA interface number. For our
# 4-CDC composite the trailing digits are 1, 3, 5, 7 — control IFs
# 0, 2, 4, 6 (data = control + 1 per the CDC class spec). The middle
# `<...>` is a serial/bus hash that varies across boots, so we just
# anchor on `.usbmodem` and capture the very last digit. Single-digit
# is sufficient because our composite only exposes 8 interfaces total.
_MACOS_USBMODEM_RE = re.compile(r"\.usbmodem\d+?(\d)$")


def _get_win_pnp_id(port_name: str) -> str | None:
    try:
        import winreg
        usb_path = r"SYSTEM\CurrentControlSet\Enum\USB"
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, usb_path) as usb_key:
            num_devices = winreg.QueryInfoKey(usb_key)[0]
            for i in range(num_devices):
                dev_name = winreg.EnumKey(usb_key, i)
                try:
                    with winreg.OpenKey(usb_key, dev_name) as dev_key:
                        num_instances = winreg.QueryInfoKey(dev_key)[0]
                        for j in range(num_instances):
                            inst_name = winreg.EnumKey(dev_key, j)
                            try:
                                with winreg.OpenKey(dev_key, inst_name) as inst_key:
                                    try:
                                        with winreg.OpenKey(inst_key, "Device Parameters") as params_key:
                                            port, _ = winreg.QueryValueEx(params_key, "PortName")
                                            if port == port_name:
                                                return f"USB\\{dev_name}\\{inst_name}"
                                    except OSError:
                                        pass
                            except OSError:
                                pass
                except OSError:
                    pass
    except OSError:
        pass
    return None


def _interface_from_port(port) -> int | None:
    """Best-effort interface-number extraction from a ListPortInfo.

    Tries (in order):
      1. ``MI_XX`` in ``hwid`` (Windows convention).
      2. Trailing ``.<n>`` in ``location`` (Linux + some pyserial
         versions on macOS).
      3. ``udevadm info`` for ``ID_USB_INTERFACE_NUM`` (Linux fallback).
      4. iInterface string in ``port.interface`` matches one of the
         firmware's CDC descriptors (macOS-friendly; works anywhere
         pyserial populates the field).
      5. Trailing digit of ``/dev/cu.usbmodem<...><N>`` device name
         (macOS — `N` is the **data** interface, control = `N` - 1).
    """
    hwid = port.hwid or ""
    m = _WIN_MI_RE.search(hwid)
    if m:
        return int(m.group(1), 16)

    loc = port.location or ""
    m = _LOCATION_IFACE_RE.search(loc)
    if m:
        return int(m.group(1))

    # Windows fallback: query PNP Device ID via registry to find MI_XX
    import sys
    if sys.platform == "win32" and port.device:
        pnp_id = _get_win_pnp_id(port.device)
        if pnp_id:
            m = _WIN_MI_RE.search(pnp_id)
            if m:
                return int(m.group(1), 16)


    if shutil.which("udevadm"):
        try:
            out = subprocess.check_output(
                ["udevadm", "info", "--query=property", port.device],
                stderr=subprocess.DEVNULL,
                text=True,
            )
            for line in out.splitlines():
                if line.startswith("ID_USB_INTERFACE_NUM="):
                    return int(line.partition("=")[2].strip())
        except (subprocess.CalledProcessError, OSError):
            pass

    iface_str = (getattr(port, "interface", None) or "").strip()
    if iface_str in _INTERFACE_BY_STRING:
        return _INTERFACE_BY_STRING[iface_str]

    device = port.device or ""
    m = _MACOS_USBMODEM_RE.search(device)
    if m:
        data_iface = int(m.group(1))
        # Data interface = control + 1. Clamp at 0 so a hypothetical
        # data-IF 0 (shouldn't happen — CDC always has the control
        # pair) doesn't underflow.
        return max(0, data_iface - 1)

    return None


def discover() -> list[FaultyCatPort]:
    """Return all FaultyCat composite CDC ports, sorted by interface."""
    found: list[FaultyCatPort] = []
    for port in list_ports.comports():
        if port.vid != VID_FAULTYCAT or port.pid != PID_FAULTYCAT:
            continue
        iface = _interface_from_port(port)
        if iface is None:
            log.warning(
                "found FaultyCat CDC at %s but could not determine its "
                "interface number (hwid=%r, location=%r) — skipping",
                port.device,
                port.hwid,
                port.location,
            )
            continue
        found.append(FaultyCatPort(interface=iface, device=port.device))
    found.sort(key=lambda p: p.interface)
    return found


def cdc_for(role: Role) -> str:
    """Return the device path/name matching the requested CDC role.

    Raises:
        ValueError: ``role`` not in INTERFACE_NUMBERS.
        PortDiscoveryError: no FaultyCat CDC found at the expected
            interface (board not enumerated, or different VID/PID).
    """
    if role not in INTERFACE_NUMBERS:
        raise ValueError(f"unknown role {role!r}; expected one of {sorted(INTERFACE_NUMBERS)}")
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
