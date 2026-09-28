#!/usr/bin/env bash
#
# ESP32 Solar-Powered Thermometer Logger — container task runner.
#
# Every build/flash/test/lint task runs inside the ESP-IDF dev container so the
# host stays clean and the toolchain is reproducible. This wrapper is the single
# entry point used by the CLion run configurations (see .idea/workspace.xml) and
# the AGENTS.md workflow, so `docker compose` specifics live in one place.
#
# Usage:
#   scripts/dev.sh [build|flash|monitor|menuconfig|test|format|format-check|shell]
#
# Environment:
#   ESP32_PORT   Serial device for flash/monitor (default: /dev/ttyUSB0)
#
set -euo pipefail

# Always operate from the repository root, regardless of the caller's cwd.
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${REPO_ROOT}"

PORT="${ESP32_PORT:-/dev/ttyUSB0}"

# Run a command inside the `dev` service defined by docker-compose.yml. The
# service is created on demand and removed afterwards (--rm), matching the
# documented `docker compose run --rm dev <cmd>` workflow.
run() {
    docker compose run --rm dev "$@"
}

case "${1:-build}" in
    build)
        # Build the firmware for the configured chip target (esp32).
        run idf.py build
        ;;
    flash)
        # Flash then attach the serial monitor; exit it with Ctrl+].
        run idf.py -p "${PORT}" flash monitor
        ;;
    monitor)
        run idf.py -p "${PORT}" monitor
        ;;
    menuconfig)
        # Interactive Kconfig UI; edits sdkconfig (generated from defaults).
        run idf.py menuconfig
        ;;
    test)
        # Host-based Unity tests on the ESP-IDF `linux` target.
        run bash -lc 'cd test && idf.py --preview set-target linux && idf.py build && ./build/esp32_sensors_tests.elf'
        ;;
    format)
        # Apply the committed .clang-format (Google base, 4-space, 80 cols).
        run bash -lc 'clang-format -i main/*.cpp main/include/*.hpp test/main/*.cpp'
        ;;
    format-check)
        # CI-friendly check; non-zero exit if anything is unformatted.
        run bash -lc 'clang-format --dry-run --Werror main/*.cpp main/include/*.hpp test/main/*.cpp'
        ;;
    shell)
        # Interactive login shell with the ESP-IDF environment sourced.
        run bash -l
        ;;
    *)
        echo "usage: $0 [build|flash|monitor|menuconfig|test|format|format-check|shell]" >&2
        exit 2
        ;;
esac
