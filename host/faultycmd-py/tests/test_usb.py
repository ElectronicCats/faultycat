"""Unit tests for faultycmd.usb — port discovery + role mapping.

Mocks pyserial's list_ports.comports() so tests pass on any
platform (don't depend on a real /dev/ttyACM* or COMx).
"""

from __future__ import annotations

from dataclasses import dataclass

import pytest

from faultycmd import usb


@dataclass
class FakePort:
    """Minimal stand-in for pyserial's ListPortInfo."""

    device: str
    vid: int | None
    pid: int | None
    hwid: str = ""
    location: str | None = None
    interface: str | None = None


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
        hwid=(f"USB VID:PID={vid:04X}:{pid:04X} SER=FLT3-XXXX " f"LOCATION=1-3:x.{iface}"),
        location=f"1-3:x.{iface}",
    )


def _macos_port(
    dev: str,
    vid: int,
    pid: int,
    iface_string: str | None = None,
) -> FakePort:
    """Real macOS pyserial output (verified on a MacBook against
    firmware vX.Y.Z.W): location is just the bus topology
    (`20-2`), hwid has no MI_XX, and the interface number lives in
    the trailing digit of the device name.

    `iface_string` simulates pyserial populating `port.interface`
    with the firmware's iInterface descriptor — None reproduces the
    fallback path (parse the device name)."""
    return FakePort(
        device=dev,
        vid=vid,
        pid=pid,
        hwid=(
            f"USB VID:PID={vid:04X}:{pid:04X} "
            "SER=FLT3-E6633C805B3A3827 LOCATION=20-2"
        ),
        location="20-2",
        interface=iface_string,
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
def macos_environment(monkeypatch):
    """The exact shape from the reported macOS bug: 4 FaultyCat CDCs
    with device names `/dev/cu.usbmodem142{01,03,05,07}`, location
    `20-2` (no `.<n>` suffix), and pyserial NOT populating
    `port.interface`. The trailing digit on the device name is the
    only signal available — this fixture exercises that fallback."""
    ports = [
        _macos_port("/dev/cu.usbmodem14201", 0x1209, 0xFA17),
        _macos_port("/dev/cu.usbmodem14203", 0x1209, 0xFA17),
        _macos_port("/dev/cu.usbmodem14205", 0x1209, 0xFA17),
        _macos_port("/dev/cu.usbmodem14207", 0x1209, 0xFA17),
        # An unrelated USB-modem on the same Mac, NOT FaultyCat.
        _macos_port("/dev/cu.usbmodem14301", 0x2341, 0x0043),
    ]
    monkeypatch.setattr("faultycmd.usb.list_ports.comports", lambda: ports)
    monkeypatch.setattr("shutil.which", lambda _name: None)
    return ports


@pytest.fixture
def macos_with_iinterface_environment(monkeypatch):
    """Variant of the macOS shape where pyserial DOES surface the
    iInterface descriptor in `port.interface`. The string-match
    fallback should fire before the device-name parse."""
    ports = [
        _macos_port("/dev/cu.usbmodem14201", 0x1209, 0xFA17, "FaultyCat EMFI Control"),
        _macos_port("/dev/cu.usbmodem14203", 0x1209, 0xFA17, "FaultyCat Crowbar Control"),
        _macos_port("/dev/cu.usbmodem14205", 0x1209, 0xFA17, "FaultyCat Scanner Shell"),
        _macos_port("/dev/cu.usbmodem14207", 0x1209, 0xFA17, "FaultyCat Target UART"),
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


# -- macOS — device-name fallback (no port.interface populated) ------


def test_discover_macos_finds_only_faultycat(macos_environment):
    ports = usb.discover()
    assert len(ports) == 4
    assert {p.interface for p in ports} == {0, 2, 4, 6}
    # The Arduino-like usbmodem14301 must NOT come through.
    assert all("14301" not in p.device for p in ports)


def test_cdc_for_emfi_macos(macos_environment):
    # `usbmodem14201` has trailing digit 1 → data IF 1 → control IF 0.
    assert usb.cdc_for("emfi") == "/dev/cu.usbmodem14201"


def test_cdc_for_crowbar_macos(macos_environment):
    assert usb.cdc_for("crowbar") == "/dev/cu.usbmodem14203"


def test_cdc_for_scanner_macos(macos_environment):
    assert usb.cdc_for("scanner") == "/dev/cu.usbmodem14205"


def test_cdc_for_target_macos(macos_environment):
    assert usb.cdc_for("target") == "/dev/cu.usbmodem14207"


# -- macOS — iInterface-string fallback ------------------------------


def test_discover_macos_uses_iinterface_string(macos_with_iinterface_environment):
    ports = usb.discover()
    assert len(ports) == 4
    assert {p.interface for p in ports} == {0, 2, 4, 6}


def test_cdc_for_emfi_macos_via_iinterface(macos_with_iinterface_environment):
    assert usb.cdc_for("emfi") == "/dev/cu.usbmodem14201"


def test_cdc_for_target_macos_via_iinterface(macos_with_iinterface_environment):
    assert usb.cdc_for("target") == "/dev/cu.usbmodem14207"


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
