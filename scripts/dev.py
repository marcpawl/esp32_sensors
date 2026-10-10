#!/usr/bin/env python3
#
# ESP32 Solar-Powered Thermometer Logger — container task runner.
#
# Every build/flash/test/lint task runs inside the ESP-IDF dev container so the
# host stays clean and the toolchain is reproducible. This wrapper is the single
# entry point used by the CLion run configurations (see
# .idea/runConfigurations/*.xml) and the AGENTS.md workflow, so `docker compose`
# specifics live in one place.
#
# Usage:
#   scripts/dev.py [build|flash|monitor|menuconfig|test|format|format-check|\
#                   format-check-staged|install-hooks|shell]
#
# Environment:
#   ESP32_PORT   Serial device for flash/monitor (default: /dev/ttyUSB0)
#
"""Container task runner for the ESP32 thermometer logger."""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

# Always operate from the repository root, regardless of the caller's cwd.
REPO_ROOT = Path(__file__).resolve().parent.parent

PORT = os.environ.get("ESP32_PORT", "/dev/ttyUSB0")

# C/C++ sources checked/formatted by clang-format, discovered via git so the
# list always matches the tracked files (no missing-glob errors). Keep the
# directory filter in sync with the pre-commit hook (.githooks/pre-commit).
GIT_LS_FILES = (
    'git ls-files "main/*" "test/*" | grep -E "\\.(cpp|hpp|h|cc|cxx)$"'
)

# Directories whose staged C/C++ files format-check-staged inspects. Keep in
# sync with scripts against .clang-format and the pre-commit hook.
ROOTS = ("main", "test")
EXTENSIONS = (".cpp", ".hpp", ".h", ".cc", ".cxx")

# Docker dev image that ships the pinned clang-format version.
DEV_IMAGE = "esp32-sensors-dev:latest"

FORMAT_ARGS = ("--dry-run", "--Werror")

USAGE = (
    "usage: dev.py [build|flash|monitor|menuconfig|test|format|format-check|"
    "format-check-staged|install-hooks|shell]"
)


def run(*cmd: str) -> int:
    """Run a command in the repo root, inheriting stdio; return exit code."""
    return subprocess.run(cmd, cwd=REPO_ROOT).returncode


def compose(*args: str) -> int:
    """Run a command inside the `dev` service defined by docker-compose.yml.

    The service is created on demand and removed afterwards (--rm), matching
    the documented `docker compose run --rm dev <cmd>` workflow.
    """
    return run("docker", "compose", "run", "--rm", "dev", *args)


def compose_bash(script: str) -> int:
    """Run a bash -lc script inside the dev container."""
    return compose("bash", "-lc", script)


def git(*args: str) -> subprocess.CompletedProcess:
    """Run git in the repo root and capture output."""
    return subprocess.run(
        ("git", *args),
        cwd=REPO_ROOT,
        check=True,
        capture_output=True,
        text=True,
    )


def cmd_build() -> int:
    """Build the firmware for the configured chip target (esp32)."""
    return compose("idf.py", "build")


def cmd_flash() -> int:
    """Flash then attach the serial monitor; exit it with Ctrl+]."""
    return compose("idf.py", "-p", PORT, "flash", "monitor")


def cmd_monitor() -> int:
    """Attach the serial monitor; exit it with Ctrl+]."""
    return compose("idf.py", "-p", PORT, "monitor")


def cmd_menuconfig() -> int:
    """Interactive Kconfig UI; edits sdkconfig (generated from defaults)."""
    return compose("idf.py", "menuconfig")


def cmd_test() -> int:
    """Host-based Unity tests on the ESP-IDF `linux` target."""
    return compose_bash(
        "cd test && idf.py --preview set-target linux && idf.py build && "
        "./build/esp32_sensors_tests.elf"
    )


def cmd_format() -> int:
    """Apply the committed .clang-format (Google base, 4-space, 80 cols)."""
    return compose_bash(f"{GIT_LS_FILES} | xargs clang-format -i")


def cmd_format_check() -> int:
    """CI-friendly check; non-zero exit if anything is unformatted."""
    return compose_bash(f"{GIT_LS_FILES} | xargs clang-format --dry-run --Werror")


def cmd_install_hooks() -> int:
    """Activate the version-controlled git hooks (.githooks/pre-commit).

    The hook rejects commits whose staged C/C++ files are not
    clang-formatted.
    """
    rc = run("git", "-C", str(REPO_ROOT), "config", "core.hooksPath", ".githooks")
    if rc == 0:
        print("Installed git hooks: core.hooksPath -> .githooks")
    return rc


def cmd_shell() -> int:
    """Interactive login shell with the ESP-IDF environment sourced."""
    return compose("bash", "-l")


def _staged_files() -> list[str]:
    """Staged, added/copied/modified C/C++ files (deleted files skipped)."""
    out = git(
        "diff",
        "--cached",
        "--name-only",
        "--diff-filter=ACMR",
        "--",
        *ROOTS,
    ).stdout
    return [line for line in out.splitlines() if line.endswith(EXTENSIONS)]


def _materialize_staged(files: list[str], tmp_dir: Path) -> None:
    """Write the staged (index) version of each file into a temp tree.

    We check exactly what is being committed rather than the working copy. The
    committed .clang-format is copied in too so clang-format discovers it (it
    searches upward from each source file). Copying beats bind-mounting a
    single file, which fails when the parent is mounted read-only.
    """
    for f in files:
        dest = tmp_dir / f
        dest.parent.mkdir(parents=True, exist_ok=True)
        blob = subprocess.run(
            ("git", "show", f":{f}"),
            cwd=REPO_ROOT,
            check=True,
            capture_output=True,
        ).stdout
        dest.write_bytes(blob)
    shutil.copyfile(REPO_ROOT / ".clang-format", tmp_dir / ".clang-format")


def _run_staged_format_check(tmp_dir: Path, files: list[str]) -> int:
    """Run clang-format --dry-run --Werror, preferring the host tool."""
    if shutil.which("clang-format"):
        return subprocess.run(
            ("clang-format", *FORMAT_ARGS, *files),
            cwd=tmp_dir,
        ).returncode
    return run(
        "docker",
        "run",
        "--rm",
        "-v",
        f"{tmp_dir}:/check:ro",
        "-w",
        "/check",
        "--entrypoint",
        "clang-format",
        DEV_IMAGE,
        *FORMAT_ARGS,
        *files,
    )


def cmd_format_check_staged() -> int:
    """Check only the staged C/C++ content (used by the pre-commit hook).

    Unlike `format-check`, this inspects the *index* version of the staged
    files, so it never complains about unrelated working-tree edits and always
    validates exactly what is about to be committed.
    """
    files = _staged_files()
    if not files:
        return 0

    if not shutil.which("clang-format") and not shutil.which("docker"):
        print(
            "format-check-staged: clang-format not found "
            "(host or container); skipping.",
            file=sys.stderr,
        )
        return 0

    with tempfile.TemporaryDirectory() as tmp:
        tmp_dir = Path(tmp)
        _materialize_staged(files, tmp_dir)
        rc = _run_staged_format_check(tmp_dir, files)

    if rc == 0:
        return 0

    print("", file=sys.stderr)
    print(
        "format-check-staged: clang-format check FAILED — these staged files "
        "need formatting:",
        file=sys.stderr,
    )
    print("", file=sys.stderr)
    print("Fix with:  scripts/dev.py format", file=sys.stderr)
    print("Or bypass (not recommended):  git commit --no-verify", file=sys.stderr)
    return 1


COMMANDS = {
    "build": cmd_build,
    "flash": cmd_flash,
    "monitor": cmd_monitor,
    "menuconfig": cmd_menuconfig,
    "test": cmd_test,
    "format": cmd_format,
    "format-check": cmd_format_check,
    "format-check-staged": cmd_format_check_staged,
    "install-hooks": cmd_install_hooks,
    "shell": cmd_shell,
}


def main(argv: list[str]) -> int:
    command = argv[1] if len(argv) > 1 else "build"
    handler = COMMANDS.get(command)
    if handler is None:
        print(USAGE, file=sys.stderr)
        return 2
    return handler()


if __name__ == "__main__":
    sys.exit(main(sys.argv))
