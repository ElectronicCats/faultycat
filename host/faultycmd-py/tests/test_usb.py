"""Unit tests for faultycmd.usb — port discovery + role mapping.

Mocks pyserial's list_ports.comports() so tests pass on any
platform (don't depend on a real /dev/ttyACM* or COMx).
"""
from __future__ import annotations

from dataclasses import dataclass
from typing import Optional

import pytest

from faultycmd import usb


@dataclass
class FakePort:
    """Minimal stand-in for pyserial's ListPortInfo."""

    device: str
    vid: Optional[int]
    pid: Optional[int]
    hwid: str = ""
    location: Optional[str] = None


def _linux_port(dev: str, vid: int, pid: int, iface: int) -> FakePort:
    return FakePort(
        device=dev,
        vid=vid,
        pid=pid,
        hwid=f"USB VID:PID={vid:04X}:{pid:04X}",
        location=f"1-3:1.{iface}",
    )


def _windows_port(dev: str, vid: int, pid: int, iface: int) -> FakePort:
    # Real Windows pyserial output for usbser.sys-backed composite CDCs
    # exposes the interface number via the location's trailing ".N".
    # The hwid string has the same VID:PID=... LOCATION=... shape as
    # Linux (no MI_XX token), so we rely on location parsing.
    return FakePort(
        device=dev,
        vid=vid,
        pid=pid,
        hwid=(
            f"USB VID:PID={vid:04X}:{pid:04X} SER=FLT3-XXXX "
            f"LOCATION=1-3:x.{iface}"
        ),
        location=f"1-3:x.{iface}",
    )


@pytest.fixture
def linux_environment(monkeypatch):
    """ttyACM0..3 belong to FaultyCat at IF 0/2/4/6, ttyACM4 is FTDI."""
    ports = [
        _linux_port("/dev/ttyACM0", 0x1209, 0xFA17, 0x00),
        _linux_port("/dev/ttyACM1", 0x1209, 0xFA17, 0x02),
        _linux_port("/dev/ttyACM2", 0x1209, 0xFA17, 0x04),
        _linux_port("/dev/ttyACM3", 0x1209, 0xFA17, 0x06),
        _linux_port("/dev/ttyACM4", 0x0403, 0x6001, 0x00),
    ]
    monkeypatch.setattr("faultycmd.usb.list_ports.comports", lambda: ports)
    monkeypatch.setattr("shutil.which", lambda _name: None)
    return ports


@pytest.fixture
def windows_environment(monkeypatch):
    """COM3..6 belong to FaultyCat at IF 0/2/4/6, COM7 is a USB-UART."""
    ports = [
        _windows_port("COM3", 0x1209, 0xFA17, 0x00),
        _windows_port("COM4", 0x1209, 0xFA17, 0x02),
        _windows_port("COM5", 0x1209, 0xFA17, 0x04),
        _windows_port("COM6", 0x1209, 0xFA17, 0x06),
        _windows_port("COM7", 0x10C4, 0xEA60, 0x00),  # CP2102
    ]
    monkeypatch.setattr("faultycmd.usb.list_ports.comports", lambda: ports)
    monkeypatch.setattr("shutil.which", lambda _name: None)
    return ports


def test_discover_linux_finds_only_faultycat(linux_environment):
    ports = usb.discover()
    assert len(ports) == 4
    assert {p.interface for p in ports} == {0, 2, 4, 6}
    assert all("ttyACM4" not in p.device for p in ports)


def test_discover_windows_finds_only_faultycat(windows_environment):
    ports = usb.discover()
    assert len(ports) == 4
    assert {p.interface for p in ports} == {0, 2, 4, 6}
    assert all(p.device != "COM7" for p in ports)


def test_discover_sorted_by_interface(linux_environment):
    ports = usb.discover()
    assert [p.interface for p in ports] == [0, 2, 4, 6]


def test_cdc_for_emfi_linux(linux_environment):
    assert usb.cdc_for("emfi") == "/dev/ttyACM0"


def test_cdc_for_crowbar_linux(linux_environment):
    assert usb.cdc_for("crowbar") == "/dev/ttyACM1"


def test_cdc_for_scanner_linux(linux_environment):
    assert usb.cdc_for("scanner") == "/dev/ttyACM2"


def test_cdc_for_target_linux(linux_environment):
    assert usb.cdc_for("target") == "/dev/ttyACM3"


def test_cdc_for_emfi_windows(windows_environment):
    assert usb.cdc_for("emfi") == "COM3"


def test_cdc_for_crowbar_windows(windows_environment):
    assert usb.cdc_for("crowbar") == "COM4"


def test_cdc_for_scanner_windows(windows_environment):
    assert usb.cdc_for("scanner") == "COM5"


def test_cdc_for_target_windows(windows_environment):
    assert usb.cdc_for("target") == "COM6"


def test_cdc_for_unknown_role_raises(linux_environment):
    with pytest.raises(ValueError):
        usb.cdc_for("dap")  # type: ignore[arg-type]


def test_cdc_for_no_match_raises(monkeypatch):
    monkeypatch.setattr("faultycmd.usb.list_ports.comports", lambda: [])
    monkeypatch.setattr("shutil.which", lambda _name: None)
    with pytest.raises(usb.PortDiscoveryError):
        usb.cdc_for("emfi")


def test_no_devices_returns_empty(monkeypatch):
    monkeypatch.setattr("faultycmd.usb.list_ports.comports", lambda: [])
    monkeypatch.setattr("shutil.which", lambda _name: None)
    assert usb.discover() == []


def test_interface_numbers_match_firmware_spec():
    assert usb.INTERFACE_NUMBERS == {
        "emfi": 0x00,
        "crowbar": 0x02,
        "scanner": 0x04,
        "target": 0x06,
    }
