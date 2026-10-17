cmake_minimum_required(VERSION 3.22)

# `mcgowan` — the ESP32 Solar-Powered Thermometer Logger firmware.
#
# This is the ESP-IDF project that produces the flashable application image
# `mcgowan.bin` (the binary name derives from the project name below). It is a
# standalone project whose `main/` component holds the application sources in
# `mcgowan/src/`; the reusable HAL components are shared from the repo-root
# `components/` directory.
#
# The repo root is NOT an ESP-IDF project: it only builds the subprojects
# (`mcgowan/`, `qemu/`, `test/`). Build this one on its own with:
#   scripts/dev.py build
# or directly in the container:
#   scripts/dev.py shell -C mcgowan idf.py -B build_esp32s3 build

# Fail fast when building on the host instead of inside the dev container.
# The Dockerfile exports DOCKER_IMAGE=esp32_sensors (see the ENV at its end);
# requiring it keeps the toolchain reproducible and boxes the build/format/
# test workflow into the container documented in AGENTS.md.
if(NOT "$ENV{DOCKER_IMAGE}" STREQUAL "esp32_sensors")
    message(FATAL_ERROR
        "This project must be built inside the ESP-IDF dev container.\n"
        "DOCKER_IMAGE is not 'esp32_sensors', so this looks like a host "
        "invocation.\n"
        "Use:  scripts/dev.py build\n"
        "or:   docker compose run --rm dev idf.py -C mcgowan "
        "-B mcgowan/build_esp32s3 build")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Reusable HAL components live in the repo-root `components/` directory, which
# is not on this project's default search path (that would be
# `mcgowan/components/`). Point at it so `hal_board` (and its `hal_common`
# dependency) are found.
list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../components")

# This project links `hal_board` (GPIO-backed drivers). `hal_stub` exposes the
# same headers and defines the same symbols, so exclude it to keep exactly one
# implementation in view. Must be set before project().
set(EXCLUDE_COMPONENTS hal_stub)

# The ELF/app binary name derives from the project name: mcgowan.bin.
project(mcgowan)
