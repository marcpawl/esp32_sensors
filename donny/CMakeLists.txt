cmake_minimum_required(VERSION 3.22)

# `donny` — linux-emulated application.
#
# A standalone ESP-IDF project pinned to the `linux` target: it builds and runs
# natively on the host (inside the dev container), needing no ESP32 hardware.
# Unlike the host *test* projects it is a plain application (its `app_main`
# simply spins on `vTaskDelay`), not a test runner.
#
# The point of the project is to link the *production* HAL — `hal_board`, not
# `hal_stub` — so the same driver-facing code paths can be exercised under the
# linux emulation. It requires `hal_board`, `espidf_cpp`, and `logic`.
#
# Build and run with:
#   scripts/dev.py build
# or directly in the container:
#   scripts/dev.py shell -C donny idf.py -B build_linux \
#       --preview set-target linux
#   scripts/dev.py shell -C donny idf.py -B build_linux build
#   ./donny/build_linux/donny.elf
#
# The build directory is named after the target (`build_linux`), like the other
# subprojects'.

# Fail fast when building on the host instead of inside the dev container.
# The Dockerfile exports DOCKER_IMAGE=esp32_sensors (see the ENV at its end);
# requiring it keeps the toolchain reproducible and boxes the build/format/
# test workflow into the container documented in AGENTS.md.
if(NOT "$ENV{DOCKER_IMAGE}" STREQUAL "esp32_sensors")
    message(FATAL_ERROR
        "The donny project must be built inside the ESP-IDF dev container.\n"
        "DOCKER_IMAGE is not 'esp32_sensors', so this looks like a host "
        "invocation.\n"
        "Use:  scripts/dev.py build")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# The reusable components (`hal_stub`, `hal_common`, `logic`, `espidf_cpp`)
# live in the repo-root `components/` directory, which is not on this project's
# default search path (that would be `donny/components/`). Point at it.
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../components")

# Restrict the component set to `main` (and its requirements) plus `hal_stub`.
# Without this the `linux` target also tries to build chip-specific components
# such as esp_driver_ana_cmpr, which have no linux implementation and fail to
# resolve. `hal_stub` is listed explicitly because it is a requirement of
# `main`; `hal_board` is deliberately excluded (the two are mutually exclusive,
# and the GPIO-backed drivers are not linkable on linux).
set(COMPONENTS main hal_stub)

# This project links `hal_stub`. `hal_board` exposes the same headers and
# defines the same symbols, so exclude it to keep exactly one implementation in
# view. Must be set before project().
set(EXCLUDE_COMPONENTS hal_board)

# The ELF/app binary name derives from the project name: donny.elf.
project(donny)
