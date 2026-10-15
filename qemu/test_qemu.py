#!/usr/bin/env python3
"""QEMU smoke test for the standalone `qemu/` project (ESP32-S3).

This program cleans the build directory, builds the QEMU smoke-test firmware
for the ESP32-S3 target inside the ESP-IDF dev container, boots it under the
ESP-IDF QEMU fork (`idf.py qemu` -> `qemu-system-xtensa -M esp32s3`), and
verifies that the hello_world output is printed. It exits non-zero on failure,
so it is CI-friendly.

Everything runs inside the dev container defined by `docker-compose.yml`
(`esp32-sensors-dev:latest`), which installs the ESP-IDF QEMU fork via
`idf_tools.py install qemu-xtensa` (see the Dockerfile). The host needs no
ESP-IDF or QEMU.

Usage:
    python3 test_qemu.py

See https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/tools/qemu.html
"""

from __future__ import annotations

from pathlib import Path
import shutil
import subprocess
import sys

# --- Layout -----------------------------------------------------------------

# This file lives in `qemu/`; the repo root is its parent.
PROG_DIR = Path(__file__).resolve().parent
REPO_ROOT = PROG_DIR.parent
BUILD_DIR = PROG_DIR / "build"

# Directory (relative to REPO_ROOT) that is bind-mounted into the container.
PROJECT_MOUNT = "/project"
QEMU_DIR_IN_CONTAINER = f"{PROJECT_MOUNT}/qemu"
BUILD_DIR_IN_CONTAINER = f"{QEMU_DIR_IN_CONTAINER}/build"

# --- Container --------------------------------------------------------------

# Toolchain container (has idf.py + esptool + the ESP-IDF QEMU fork).
DEV_SERVICE = "dev"

# --- Test parameters --------------------------------------------------------

# ESP32-S3 target. The ESP-IDF QEMU fork models it as `-M esp32s3`.
IDF_TARGET = "esp32s3"

# Message that proves the firmware booted and ran `app_main`.
EXPECTED_OUTPUT = "Hello world!"

# Upper bound (seconds) for the QEMU run before we stop it and inspect output.
QEMU_TIMEOUT_S = 30


def info(msg: str) -> None:
    print(f"\n==> {msg}", flush=True)


def run(cmd: list[str], cwd: Path | None = None) -> subprocess.CompletedProcess:
    """Run a command, inheriting stdio.

    Returns the CompletedProcess so callers can inspect the exit code.
    """
    print(f"$ {' '.join(cmd)}", flush=True)
    return subprocess.run(cmd, cwd=cwd, text=True)


def clean_build() -> None:
    """Remove the build directory.

    The build tree may have been created by a container running as a different
    user (e.g. root), so a plain host-side rmtree can fail with EPERM. When
    that happens, remove it from inside the dev container as root.
    """
    info(f"Cleaning build directory: {BUILD_DIR}")
    if not BUILD_DIR.exists():
        return
    try:
        shutil.rmtree(BUILD_DIR)
    except PermissionError:
        print("Host rmtree failed (permissions); removing via container root")
        rc = run(
            [
                "docker", "compose", "run", "--rm", "--user", "root",
                DEV_SERVICE, "bash", "-c",
                f"rm -rf {BUILD_DIR_IN_CONTAINER}",
            ],
            cwd=REPO_ROOT,
        )
        if rc.returncode != 0 or BUILD_DIR.exists():
            print("ERROR: could not remove the build directory", file=sys.stderr)
            sys.exit(1)


def build_in_dev_container() -> None:
    """Build the firmware for the ESP32-S3 target inside the dev container."""
    info(f"Building {IDF_TARGET} firmware in the dev container")
    rc = run(
        [
            "docker", "compose", "run", "--rm", DEV_SERVICE, "bash", "-c",
            f"cd {QEMU_DIR_IN_CONTAINER} && idf.py build",
        ],
        cwd=REPO_ROOT,
    )
    if rc.returncode != 0:
        print("ERROR: firmware build failed", file=sys.stderr)
        sys.exit(1)


def run_qemu_in_dev() -> subprocess.CompletedProcess:
    """Boot the image with the ESP-IDF QEMU fork inside the dev container.

    `idf.py qemu` builds `qemu_flash.bin` from `flash_args` (sized to match
    CONFIG_ESPTOOLPY_FLASHSIZE) and starts `qemu-system-xtensa -M esp32s3`.

    QEMU does not exit on its own (the firmware restarts in a loop), so wrap it
    in `timeout -k 5 N` (SIGTERM at N seconds, SIGKILL 5 seconds later) and
    inspect whatever it printed.
    """
    info("Running QEMU in the dev container")
    cmd = [
        "docker", "compose", "run", "--rm", DEV_SERVICE, "bash", "-c",
        f"cd {QEMU_DIR_IN_CONTAINER} && "
        f"timeout -k 5 {QEMU_TIMEOUT_S} idf.py qemu",
    ]
    try:
        return subprocess.run(
            cmd,
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            timeout=QEMU_TIMEOUT_S,
        )
    except subprocess.TimeoutExpired as exc:
        # A timeout is expected: the firmware never exits. Reconstruct a
        # CompletedProcess-like result from whatever output was captured.
        out = exc.stdout or ""
        err = exc.stderr or ""
        if isinstance(out, bytes):
            out = out.decode(errors="replace")
        if isinstance(err, bytes):
            err = err.decode(errors="replace")
        return subprocess.CompletedProcess(cmd, returncode=0, stdout=out, stderr=err)


def check_output(result: subprocess.CompletedProcess) -> bool:
    """Verify EXPECTED_OUTPUT appeared in QEMU's output."""
    output = (result.stdout or "") + (result.stderr or "")
    print(output)
    if EXPECTED_OUTPUT in output:
        return True
    print(f"\nERROR: expected output not found: {EXPECTED_OUTPUT!r}",
          file=sys.stderr)
    return False


def main() -> int:
    if len(sys.argv) > 1 and sys.argv[1] not in ("-h", "--help"):
        print(f"ERROR: unknown argument {sys.argv[1]!r}; no arguments expected",
              file=sys.stderr)
        return 2

    clean_build()
    build_in_dev_container()

    result = run_qemu_in_dev()

    info("Checking QEMU output")
    if check_output(result):
        info(f"PASS: saw {EXPECTED_OUTPUT!r}")
        return 0
    print("FAIL", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
