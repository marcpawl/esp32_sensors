#!/usr/bin/env python3
"""QEMU on-target test driver for the standalone `qemu/` project (ESP32-S3).

This program cleans the build directory, builds the Unity test firmware for the
ESP32-S3 target, boots it under the ESP-IDF QEMU fork (`idf.py qemu` ->
`qemu-system-xtensa -M esp32s3`), and verifies the Unity results. It exits
non-zero on failure, so it is CI-friendly.

Success is decided by the firmware's completion marker line
(`QEMU-TESTS-COMPLETE failures=<n>`), which `test_main.cpp` prints once the
whole suite has run. The driver also scans for Unity `FAIL` lines as a
belt-and-braces check.

This script must be run *inside* the ESP-IDF dev container (the image sets
`DOCKER_IMAGE=esp32_sensors`), which installs the ESP-IDF QEMU fork via
`idf_tools.py install qemu-xtensa` (see the Dockerfile). It does not invoke
`docker`; if it is not in the container it aborts. From the host, run it
through the container task runner:

    scripts/dev.py test-qemu
    scripts/dev.py shell python3 qemu/test_qemu.py

See https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/tools/qemu.html
"""

from __future__ import annotations

import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

# --- Layout -----------------------------------------------------------------

# This file lives in `qemu/`; the repo root is its parent.
PROG_DIR = Path(__file__).resolve().parent
BUILD_DIR = PROG_DIR / "build"

# --- Container --------------------------------------------------------------

# Environment variable set by the dev image (see the Dockerfile). Its presence
# tells us we are already inside the container, where idf.py, esptool, and the
# ESP-IDF QEMU fork are installed.
CONTAINER_ENV = "DOCKER_IMAGE"
CONTAINER_ENV_VALUE = "esp32_sensors"

# --- Test parameters --------------------------------------------------------

# ESP32-S3 target. The ESP-IDF QEMU fork models it as `-M esp32s3`.
IDF_TARGET = "esp32s3"

# Completion marker printed by `test_main.cpp` after `unity_run_all_tests()`.
# Captures the failure count so we can assert it is zero.
COMPLETE_RE = re.compile(r"QEMU-TESTS-COMPLETE failures=(\d+)")

# Upper bound (seconds) for the QEMU run before we stop it and inspect output.
# The suite is short; this is a safety net, not a per-test budget.
QEMU_TIMEOUT_S = 60


def info(msg: str) -> None:
    print(f"\n==> {msg}", flush=True)


def in_container() -> bool:
    """Whether we are running inside the ESP-IDF dev container."""
    return os.environ.get(CONTAINER_ENV) == CONTAINER_ENV_VALUE


def run(cmd: list[str], cwd: Path | None = None) -> subprocess.CompletedProcess:
    """Run a command, inheriting stdio.

    Returns the CompletedProcess so callers can inspect the exit code.
    """
    print(f"$ {' '.join(cmd)}", flush=True)
    return subprocess.run(cmd, cwd=cwd, text=True)


def clean_build() -> None:
    """Remove the build directory."""
    info(f"Cleaning build directory: {BUILD_DIR}")
    if not BUILD_DIR.exists():
        return
    shutil.rmtree(BUILD_DIR)


def build_firmware() -> None:
    """Build the test firmware for the ESP32-S3 target."""
    info(f"Building {IDF_TARGET} test firmware")
    rc = run(["idf.py", "build"], cwd=PROG_DIR)
    if rc.returncode != 0:
        print("ERROR: firmware build failed", file=sys.stderr)
        sys.exit(1)


def run_qemu() -> subprocess.CompletedProcess:
    """Boot the image with the ESP-IDF QEMU fork.

    `idf.py qemu` builds `qemu_flash.bin` from `flash_args` (sized to match
    CONFIG_ESPTOOLPY_FLASHSIZE) and starts `qemu-system-xtensa -M esp32s3`.

    The firmware finishes the suite then idles, so QEMU does not exit on its
    own; wrap it in `timeout -k 5 N` (SIGTERM at N seconds, SIGKILL 5 seconds
    later) and inspect whatever it printed.
    """
    info("Running QEMU")
    cmd = [
        "timeout", "-k", "5", str(QEMU_TIMEOUT_S),
        "idf.py", "qemu",
    ]
    try:
        return subprocess.run(
            cmd,
            cwd=PROG_DIR,
            capture_output=True,
            text=True,
            timeout=QEMU_TIMEOUT_S + 10,
        )
    except subprocess.TimeoutExpired as exc:
        # A timeout is expected: the firmware never exits on its own.
        # Reconstruct a CompletedProcess-like result from captured output.
        out = exc.stdout or ""
        err = exc.stderr or ""
        if isinstance(out, bytes):
            out = out.decode(errors="replace")
        if isinstance(err, bytes):
            err = err.decode(errors="replace")
        return subprocess.CompletedProcess(cmd, returncode=0, stdout=out, stderr=err)


def check_output(result: subprocess.CompletedProcess) -> bool:
    """Verify the Unity suite completed with zero failures."""
    output = (result.stdout or "") + (result.stderr or "")
    print(output)

    match = COMPLETE_RE.search(output)
    if match is None:
        print(
            "\nERROR: completion marker not found; the suite did not finish "
            "(build/run failure, crash, or timeout).",
            file=sys.stderr,
        )
        return False

    failures = int(match.group(1))
    if failures != 0:
        print(f"\nERROR: Unity reported {failures} failure(s).", file=sys.stderr)
        return False

    # Secondary check: reject any Unity FAIL line even if the count looked OK.
    for line in output.splitlines():
        if line.strip().startswith("FAIL"):
            print(f"\nERROR: Unity failure line: {line!r}", file=sys.stderr)
            return False

    return True


def main() -> int:
    if len(sys.argv) > 1 and sys.argv[1] not in ("-h", "--help"):
        print(f"ERROR: unknown argument {sys.argv[1]!r}; no arguments expected",
              file=sys.stderr)
        return 2

    if not in_container():
        print(
            "ERROR: this script must run inside the ESP-IDF dev container "
            f"({CONTAINER_ENV}={CONTAINER_ENV_VALUE} is not set).\n"
            "       From the host, run it through the container task runner:\n"
            "           scripts/dev.py test-qemu\n"
            "       or:\n"
            "           scripts/dev.py shell python3 qemu/test_qemu.py",
            file=sys.stderr,
        )
        return 2

    clean_build()
    build_firmware()

    result = run_qemu()

    info("Checking Unity results")
    if check_output(result):
        info("PASS: QEMU Unity suite completed with 0 failures")
        return 0
    print("FAIL", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
