#!/usr/bin/env python3
"""FaultyCat v3 minimal EMFI host client (F4-6).

NOT the F10 Rust client. Reference tool only — pyserial round-trip
for configure/arm/fire/status/capture across CDC0.
"""
import argparse
import struct
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("pip install pyserial")

SOF = 0xFA
CMD = {
    "ping":      0x01,
    "configure": 0x10,
    "arm":       0x11,
    "fire":      0x12,
    "disarm":    0x13,
    "status":    0x14,
    "capture":   0x15,
}
TRIG = {
    "immediate":    0,
    "ext-rising":   1,
    "ext-falling":  2,
    "ext-pulse-pos":3,
}

def crc16_ccitt(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc

def frame(cmd: int, payload: bytes = b"") -> bytes:
    body = bytes([cmd]) + struct.pack("<H", len(payload)) + payload
    crc = crc16_ccitt(body)
    return bytes([SOF]) + body + struct.pack("<H", crc)

def read_frame(ser, timeout=2.0) -> tuple[int, bytes]:
    deadline = time.time() + timeout
    while time.time() < deadline:
        b = ser.read(1)
        if b and b[0] == SOF:
            header = ser.read(3)
            if len(header) != 3: continue
            cmd = header[0]
            length = struct.unpack("<H", header[1:3])[0]
            payload = ser.read(length)
            crc_bytes = ser.read(2)
            if len(payload) != length or len(crc_bytes) != 2: continue
            expected = struct.unpack("<H", crc_bytes)[0]
            calc = crc16_ccitt(header + payload)
            if expected != calc:
                continue
            return cmd, payload
    raise TimeoutError("no reply")

def cmd_ping(ser):
    ser.write(frame(CMD["ping"]))
    _, pl = read_frame(ser)
    print(f"PONG {pl!r}")

def cmd_configure(ser, trigger, delay, width, charge):
    pl = bytes([TRIG[trigger]]) + struct.pack("<III", delay, width, charge)
    ser.write(frame(CMD["configure"], pl))
    _, r = read_frame(ser)
    print(f"configure -> err={r[0]}")

def cmd_arm(ser):
    ser.write(frame(CMD["arm"]))
    _, r = read_frame(ser); print(f"arm -> err={r[0]}")

def cmd_fire(ser, trigger_timeout):
    ser.write(frame(CMD["fire"], struct.pack("<I", trigger_timeout)))
    _, r = read_frame(ser); print(f"fire -> err={r[0]}")

def cmd_status(ser):
    ser.write(frame(CMD["status"]))
    _, r = read_frame(ser)
    state, err = r[0], r[1]
    last, fill, wus, dus = struct.unpack("<IIII", r[2:18])
    print(f"state={state} err={err} last={last}ms fill={fill} width={wus}us delay={dus}us")

def cmd_capture(ser, offset, length):
    ser.write(frame(CMD["capture"], struct.pack("<HH", offset, length)))
    _, r = read_frame(ser)
    print(f"capture[{offset}:{offset+length}] = {r.hex()}")

def main():
    p = argparse.ArgumentParser()
    p.add_argument("--port", default="/dev/ttyACM0")
    sub = p.add_subparsers(dest="cmd", required=True)
    sub.add_parser("ping")
    c = sub.add_parser("configure")
    c.add_argument("--trigger", choices=list(TRIG), required=True)
    c.add_argument("--delay", type=int, default=0)
    c.add_argument("--width", type=int, default=5)
    c.add_argument("--charge-timeout", type=int, default=1000)
    sub.add_parser("arm")
    f = sub.add_parser("fire"); f.add_argument("--trigger-timeout", type=int, default=1000)
    sub.add_parser("disarm")
    sub.add_parser("status")
    cp = sub.add_parser("capture")
    cp.add_argument("--offset", type=int, default=0)
    cp.add_argument("--length", type=int, default=64)
    args = p.parse_args()

    with serial.Serial(args.port, 115200, timeout=0.5) as ser:
        if args.cmd == "ping":       cmd_ping(ser)
        elif args.cmd == "configure":cmd_configure(ser, args.trigger, args.delay, args.width, args.charge_timeout)
        elif args.cmd == "arm":      cmd_arm(ser)
        elif args.cmd == "fire":     cmd_fire(ser, args.trigger_timeout)
        elif args.cmd == "disarm":   ser.write(frame(CMD["disarm"])); read_frame(ser); print("disarmed")
        elif args.cmd == "status":   cmd_status(ser)
        elif args.cmd == "capture":  cmd_capture(ser, args.offset, args.length)

if __name__ == "__main__":
    main()
