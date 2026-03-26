"""
m5/power.py — управление питанием таргета через M5 RLY1.

Использование:
    python m5/power.py --port /dev/ttyACM1 --state on
    python m5/power.py --port /dev/ttyACM1 --state off
"""

from __future__ import annotations

import argparse
import json
import sys
import time

import serial


def main() -> None:
    parser = argparse.ArgumentParser(description="M5 target power control")
    parser.add_argument("--port", required=True, help="M5 serial port")
    parser.add_argument("--state", required=True, choices=["on", "off"])
    args = parser.parse_args()

    state = args.state == "on"
    cmd = json.dumps({"cmd": "power", "state": state})

    ser = serial.Serial(args.port, 115200, timeout=3)
    time.sleep(0.1)
    ser.reset_input_buffer()
    ser.write(cmd.encode("ascii") + b"\r\n")

    deadline = time.monotonic() + 3
    while time.monotonic() < deadline:
        line = ser.readline().decode("ascii", errors="replace").strip()
        if not line.startswith("{"):
            continue
        resp = json.loads(line)
        if resp.get("ok"):
            label = "включено" if state else "выключено"
            print(f"  Питание таргета {label}")
            ser.close()
            return
        print(f"  M5 error: {resp.get('err', '?')}")
        ser.close()
        sys.exit(1)

    ser.close()
    print("  Нет ответа от M5")
    sys.exit(1)


if __name__ == "__main__":
    main()
