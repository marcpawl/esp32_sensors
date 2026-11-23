#!/usr/bin/env python3
"""Flash the ESP32-S3 and run the on-target libespidfcpp test suite.

A board cannot report an exit status to the host, so the executable prints a
sentinel after Unity finishes and this driver watches the serial log:

  * `idf.py flash` runs first; a failed flash ends the run immediately;
  * the run is bounded by `TIMEOUT_SECONDS` (120 s);
  * exit status is 0 only after the passing sentinel, and non-zero for a
    failing sentinel, a timeout, or a reset before the suite finishes.

Run it inside the dev container, either through
`./scripts/dev.py test-esp32 components/espidfcpp/tests/test-espidf-cpp-esp32`
or from this directory with `python3 test.py`.
"""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess
import sys
import time

try:
    import serial
    from serial.tools import list_ports
except ImportError:  # pragma: no cover - the dev image ships pyserial
    serial = None
    list_ports = None

BUILD_DIR = "build_esp32s3"
BAUD_RATE = 115200
TIMEOUT_SECONDS = 120
PASS_SENTINEL = "ESPIDF_CPP_ESP32_TESTS: PASS"
FAIL_SENTINEL = "ESPIDF_CPP_ESP32_TESTS: FAIL"
# USB vendor ID of the ESP32-S3's built-in USB-serial/JTAG bridge.
ESPRESSIF_VID = 0x303A


def find_port() -> str | None:
    """The serial device: `ESP32_PORT`, else the first Espressif port."""
    configured = os.environ.get("ESP32_PORT")
    if configured:
        return configured
    if list_ports is None:
        return None
    for port in list_ports.comports():
        if port.vid == ESPRESSIF_VID:
            return port.device
    return None


def flash(project_dir: Path, port: str) -> bool:
    """Run `idf.py flash`; return whether it succeeded."""
    command = [
        "idf.py",
        "-B",
        str(project_dir / BUILD_DIR),
        "-p",
        port,
        "flash",
    ]
    print(command, flush=True)
    result = subprocess.run(command, cwd=project_dir, check=False)
    return result.returncode == 0


def run_capture(port: str) -> int:
    """Reset the board, then read until the sentinel or the deadline."""
    try:
        link = serial.Serial(port, BAUD_RATE, timeout=0.2)
    except serial.SerialException as error:
        print(f"test.py: cannot open {port}: {error}", file=sys.stderr)
        return 1

    with link:
        # Pulse reset so the capture starts at boot, not at "already running":
        # `idf.py flash` resets the chip before this port is reopened.
        link.setDTR(False)
        link.setRTS(True)
        time.sleep(0.1)
        link.setRTS(False)

        deadline = time.monotonic() + TIMEOUT_SECONDS
        while time.monotonic() < deadline:
            line = link.readline()
            if not line:
                continue
            text = line.decode(errors="replace").rstrip("\r\n")
            print(text, flush=True)
            if PASS_SENTINEL in text:
                return 0
            if FAIL_SENTINEL in text:
                return 1

    print(
        f"test.py: no result within {TIMEOUT_SECONDS} seconds",
        file=sys.stderr,
    )
    return 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--port",
        help="serial device (default: ESP32_PORT, else auto-detect)",
    )
    args = parser.parse_args()

    if serial is None:
        print(
            "test.py: pyserial is missing; run inside the dev container",
            file=sys.stderr,
        )
        return 1

    project_dir = Path(__file__).resolve().parent
    port = args.port or find_port()
    if port is None:
        print(
            "test.py: no ESP32 serial port found; set ESP32_PORT",
            file=sys.stderr,
        )
        return 1

    if not flash(project_dir, port):
        print("test.py: idf.py flash failed", file=sys.stderr)
        return 1
    return run_capture(port)


if __name__ == "__main__":
    sys.exit(main())
