"""Unit tests for faultycmd.usb — port discovery + role mapping.

Tests mock subprocess + Path.glob so they pass on any platform
(don't depend on a real udevadm or /dev/ttyACM*).
"""
from __future__ import annotations

import pytest

from faultycmd import usb


def _udev_text(vid: str, pid: str, iface: str) -> str:
    return (
        f"ID_VENDOR_ID={vid}\n"
        f"ID_MODEL_ID={pid}\n"
        f"ID_USB_INTERFACE_NUM={iface}\n"
    )


@pytest.fixture
def fake_environment(monkeypatch):
    """Pretend /dev/ttyACM0..3 belong to FaultyCat at IF 0/2/4/6, ttyACM4 belongs to a different vendor."""
    fake_devs = ["ttyACM0", "ttyACM1", "ttyACM2", "ttyACM3", "ttyACM4"]
    udev_table = {
        "/dev/ttyACM0": _udev_text("1209", "fa17", "00"),
        "/dev/ttyACM1": _udev_text("1209", "fa17", "02"),
        "/dev/ttyACM2": _udev_text("1209", "fa17", "04"),
        "/dev/ttyACM3": _udev_text("1209", "fa17", "06"),
        "/dev/ttyACM4": _udev_text("0403", "6001", "00"),  # FTDI cable
    }

    class FakePath:
        def __init__(self, p: str):
            self.p = p

        def __str__(self) -> str:
            return self.p

        def __lt__(self, other) -> bool:
            return self.p < other.p

    def fake_glob(self, pattern):
        if pattern == "ttyACM*":
            return [FakePath(f"/dev/{d}") for d in fake_devs]
        return []

    def fake_check_output(cmd, **kwargs):
        if not (isinstance(cmd, list) and "udevadm" in cmd[0]):
            raise FileNotFoundError
        device = cmd[-1]
        text = udev_table.get(device)
        if text is None:
            raise __import__("subprocess").CalledProcessError(1, cmd)
        return text

    monkeypatch.setattr("pathlib.Path.glob", fake_glob)
    monkeypatch.setattr("subprocess.check_output", fake_check_output)
    monkeypatch.setattr("shutil.which", lambda name: "/usr/bin/udevadm" if name == "udevadm" else None)
    return udev_table


def test_discover_finds_only_faultycat(fake_environment):
    ports = usb.discover()
    assert len(ports) == 4
    assert {p.interface for p in ports} == {0, 2, 4, 6}
    # FTDI device on ACM4 must NOT appear.
    assert all("ttyACM4" not in p.device for p in ports)


def test_discover_sorted_by_interface(fake_environment):
    ports = usb.discover()
    interfaces = [p.interface for p in ports]
    assert interfaces == sorted(interfaces)


def test_cdc_for_emfi(fake_environment):
    assert usb.cdc_for("emfi") == "/dev/ttyACM0"


def test_cdc_for_crowbar(fake_environment):
    assert usb.cdc_for("crowbar") == "/dev/ttyACM1"


def test_cdc_for_scanner(fake_environment):
    assert usb.cdc_for("scanner") == "/dev/ttyACM2"


def test_cdc_for_target(fake_environment):
    assert usb.cdc_for("target") == "/dev/ttyACM3"


def test_cdc_for_unknown_role_raises(fake_environment):
    with pytest.raises(ValueError):
        usb.cdc_for("dap")  # type: ignore[arg-type]


def test_cdc_for_no_match_raises(monkeypatch):
    # Empty environment — discover() returns nothing.
    monkeypatch.setattr("pathlib.Path.glob", lambda self, pat: [])
    monkeypatch.setattr("shutil.which", lambda name: None)
    with pytest.raises(usb.PortDiscoveryError):
        usb.cdc_for("emfi")


def test_no_udevadm_returns_empty(monkeypatch):
    monkeypatch.setattr("shutil.which", lambda name: None)
    assert usb.discover() == []


def test_interface_numbers_match_firmware_spec():
    # Sanity check — the IF 0/2/4/6 layout was frozen at F3 (USB
    # composite). Drift would mis-map every CDC.
    assert usb.INTERFACE_NUMBERS == {
        "emfi": 0x00,
        "crowbar": 0x02,
        "scanner": 0x04,
        "target": 0x06,
    }
