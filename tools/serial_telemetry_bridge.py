#!/usr/bin/env python3
"""Bench bridge: parse RT-Spark sensor logs and POST telemetry to the backend.

This is a temporary local-development path while the onboard RW007 Wi-Fi
transport is not implemented. It is not part of the final firmware design.

The serial port is reopened automatically, so a USB replug or a changed COM
number does not require restarting the bridge.
"""
from __future__ import annotations

import argparse
import json
import math
import os
import re
import sys
import time
import urllib.error
import urllib.request
from datetime import datetime, timezone

import serial
from serial.tools import list_ports

ENV_RE = re.compile(
    r"env: temp=(-?\d+)\.(\d+)C rh=(\d+)\.(\d+)% light=(-?\d+)\.(\d+)lux ps=(\d+)( \(fault\))?"
)
MOTION_RE = re.compile(
    r"motion: accel=(-?\d+)/(-?\d+)/(-?\d+) gyro=(-?\d+)/(-?\d+)/(-?\d+)( \(fault\))?"
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="auto",
                        help="serial port (COM7) or 'auto' for the ST-Link VCP")
    parser.add_argument("--base-url", default="http://127.0.0.1:8000")
    parser.add_argument("--device-id", default="rt-spark-01")
    parser.add_argument("--token", default=os.getenv("MONITOR_DEVICE_TOKEN", ""))
    parser.add_argument("--interval", type=float, default=5.0, help="seconds between posts")
    parser.add_argument("--ps-obstructed", type=int, default=50,
                        help="proximity count treated as obstructed (demo only)")
    return parser.parse_args()


def resolve_port(requested: str) -> str:
    if requested.lower() != "auto":
        return requested
    candidates = [
        port.device
        for port in list_ports.comports()
        if port.vid == 0x0483 or "stlink" in (port.description or "").lower()
    ]
    if not candidates:
        raise serial.SerialException("no ST-Link VCP found")
    return candidates[0]


def post(base_url: str, device_id: str, token: str, payload: dict) -> None:
    url = f"{base_url.rstrip('/')}/api/v1/devices/{device_id}/telemetry"
    body = json.dumps(payload).encode("utf-8")
    request = urllib.request.Request(
        url,
        data=body,
        method="POST",
        headers={
            "Authorization": f"Bearer {token}",
            "Content-Type": "application/json",
            "Accept": "application/json",
        },
    )
    with urllib.request.urlopen(request, timeout=10) as response:
        response.read()


def main() -> int:
    args = parse_args()
    if not args.token:
        print("missing --token or MONITOR_DEVICE_TOKEN", file=sys.stderr)
        return 2

    latest_env: dict | None = None
    latest_motion: dict | None = None
    last_post = 0.0
    sequence = 0

    while True:
        try:
            device = resolve_port(args.port)
            port = serial.Serial(device, 115200, timeout=0.2)
        except (serial.SerialException, OSError) as error:
            print(f"bridge: waiting for serial ({error})", flush=True)
            time.sleep(2)
            continue

        print(f"bridge: {device} -> {args.base_url} ({args.device_id})", flush=True)
        try:
            while True:
                raw = port.readline()
                if not raw:
                    continue
                line = raw.decode("utf-8", errors="replace").strip()
                match = ENV_RE.search(line)
                if match:
                    latest_env = {
                        "temperature_c": float(f"{match.group(1)}.{match.group(2)}"),
                        "humidity_pct": float(f"{match.group(3)}.{match.group(4)}"),
                        "light_lux": float(f"{match.group(5)}.{match.group(6)}"),
                        "proximity": int(match.group(7)),
                        "fault": bool(match.group(8)),
                    }
                match = MOTION_RE.search(line)
                if match:
                    latest_motion = {
                        "accel": [int(match.group(i)) for i in range(1, 4)],
                        "gyro": [int(match.group(i)) for i in range(4, 7)],
                        "fault": bool(match.group(7)),
                    }

                now = time.monotonic()
                if latest_env is None or now - last_post < args.interval:
                    continue

                sequence += 1
                faults: list[str] = []
                if latest_env["fault"]:
                    faults.append("environment_read")
                if latest_motion and latest_motion["fault"]:
                    faults.append("motion_read")
                payload = {
                    "captured_at": datetime.now(timezone.utc).isoformat(),
                    "sequence": sequence,
                    "temperature_c": latest_env["temperature_c"],
                    "humidity_pct": latest_env["humidity_pct"],
                    "light_lux": latest_env["light_lux"],
                    "proximity_state": (
                        "obstructed" if latest_env["proximity"] >= args.ps_obstructed else "clear"
                    ),
                    "sensor_faults": faults,
                    "firmware_version": "bench-serial-bridge",
                }
                if latest_motion:
                    ax, ay, az = latest_motion["accel"]
                    payload["tilt_deg"] = round(math.degrees(math.atan2(ay, az)), 2)

                try:
                    post(args.base_url, args.device_id, args.token, payload)
                    print(
                        f"posted seq={sequence} temp={payload['temperature_c']} "
                        f"rh={payload['humidity_pct']} light={payload['light_lux']}",
                        flush=True,
                    )
                except (urllib.error.URLError, TimeoutError, OSError) as error:
                    print(f"post failed: {error}", file=sys.stderr, flush=True)
                last_post = now
        except (serial.SerialException, OSError) as error:
            print(f"bridge: serial lost ({error}); retrying", flush=True)
        except KeyboardInterrupt:
            return 0
        finally:
            try:
                port.close()
            except Exception:
                pass
        time.sleep(2)


if __name__ == "__main__":
    raise SystemExit(main())
