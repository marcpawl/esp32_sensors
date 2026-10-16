#!/usr/bin/env python3
"""Native (linux-target) test driver for the standalone `qemu/` project.

This program builds the Unity test firmware for the ESP-IDF `linux` target,
runs the resulting ELF directly on the host (inside the dev container), and
verifies the Unity results. It exits non-zero on failure, so it is CI-friendly.

Success is decided by the firmware's completion marker line
(`QEMU-TESTS-COMPLETE failures=<n>`), which `test_main.cpp` prints once the
whole suite has run. The driver also scans for Unity `FAIL` lines as a
belt-and-braces check.

This script must be run *inside* the ESP-IDF dev container (the image sets
`DOCKER_IMAGE=esp32_sensors`). It does not invoke `docker`; if it is not in the
container it aborts. From the host, run it through the container task runner:

    scripts/dev.py test-qemu
    scripts/dev.py shell python3 qemu/test_qemu.py
"""

from __future__ import annotations

import os
from pathlib import Path
import re
import subprocess
import sys

# --- Layout -----------------------------------------------------------------

# This file lives in `qemu/`; the repo root is its parent.
PROG_DIR = Path(__file__).resolve().parent

# Build directory is named after the target (`linux`), matching the repo-wide
# convention (firmware -> build_esp32s3, host tests -> build_linux).
BUILD_DIR = PROG_DIR / "build_linux"

# The native test ELF. Its name derives from the project name
# (`esp32_sensors_qemu`), so it ends in `_qemu`.
TEST_ELF = BUILD_DIR / "esp32_sensors_qemu.elf"

# --- Container --------------------------------------------------------------

# Environment variable set by the dev image (see the Dockerfile). Its presence
# tells us we are already inside the container, where idf.py and the ESP-IDF
# toolchain are installed.
CONTAINER_ENV = "DOCKER_IMAGE"
CONTAINER_ENV_VALUE = "esp32_sensors"

# --- Test parameters --------------------------------------------------------

# The project builds for the ESP-IDF `linux` target.
IDF_TARGET = "linux"

# Completion marker printed by `test_main.cpp` after `unity_run_all_tests()`.
# Captures the failure count so we can assert it is zero.
COMPLETE_RE = re.compile(r"QEMU-TESTS-COMPLETE failures=(\d+)")

# Upper bound (seconds) for the test run before we stop it and inspect output.
# The suite is short; this is a safety net, not a per-test budget.
RUN_TIMEOUT_S = 60


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


def configured_target() -> str | None:
    """Return the target recorded in `sdkconfig`, or None if unset.

    ESP-IDF writes `CONFIG_IDF_TARGET="<target>"` into `sdkconfig` when a target
    is selected, so reading it tells us whether `set-target` is still needed.
    """
    try:
        text = (PROG_DIR / "sdkconfig").read_text()
    except FileNotFoundError:
        return None
    match = re.search(r'^CONFIG_IDF_TARGET="([^"]+)"', text, re.MULTILINE)
    return match.group(1) if match else None


def configure_target() -> None:
    """Pin the project to the `linux` target.

    `set-target` rewrites `sdkconfig` (and forces a full rebuild), so only run
    it when the recorded target differs from `IDF_TARGET`. When it already
    matches we are a no-op, leaving every file untouched and the build
    incremental.
    """
    current = configured_target()
    if current == IDF_TARGET:
        info(f"Target already set to {IDF_TARGET}; skipping set-target")
        return
    info(f"Setting target to {IDF_TARGET} (was {current or 'unset'})")
    rc = run(["idf.py", "--preview", "-B", BUILD_DIR.name,
              "set-target", IDF_TARGET], cwd=PROG_DIR)
    if rc.returncode != 0:
        print("ERROR: set-target failed", file=sys.stderr)
        sys.exit(1)


def build_firmware() -> None:
    """Build the test ELF for the linux target."""
    info(f"Building {IDF_TARGET} test ELF")
    rc = run(["idf.py", "-B", BUILD_DIR.name, "build"], cwd=PROG_DIR)
    if rc.returncode != 0:
        print("ERROR: build failed", file=sys.stderr)
        sys.exit(1)


def run_tests() -> subprocess.CompletedProcess:
    """Run the native test ELF and capture its output.

    The firmware finishes the suite and returns from `app_main`, so the process
    exits on its own. `timeout` is still wrapped around it as a safety net in
    case a test hangs.
    """
    info("Running native test ELF")
    cmd = [
        "timeout", "-k", "5", str(RUN_TIMEOUT_S),
        str(TEST_ELF),
    ]
    try:
        return subprocess.run(
            cmd,
            cwd=PROG_DIR,
            capture_output=True,
            text=True,
            timeout=RUN_TIMEOUT_S + 10,
        )
    except subprocess.TimeoutExpired as exc:
        # A timeout means the suite did not finish; still inspect what printed.
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

    configure_target()
    build_firmware()

    result = run_tests()

    info("Checking Unity results")
    if check_output(result):
        info("PASS: native Unity suite completed with 0 failures")
        return 0
    print("FAIL", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
