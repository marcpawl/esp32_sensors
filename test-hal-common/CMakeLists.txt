cmake_minimum_required(VERSION 3.22)

# `test-hal-common` — host unit tests for the `hal_common` vocabulary.
#
# This is a standalone ESP-IDF project that targets the `linux` target only
# (never a chip): it builds the Unity test runner for the implementation-free
# contracts in `components/hal_common` and runs natively inside the dev
# container, so no ESP32 hardware is needed.
#
# The concepts name concrete device types through each implementation's public
# header (`hal/led.hpp`, `hal/wifi.hpp`, ...), so this project links the
# pin-free `hal_stub` implementation — the only one with a `linux` target. The
# tests still cover the shared vocabulary and the concept rejections; the stub
# devices themselves are tested in `test/`.
#
# Build and run with:
#   scripts/dev.py test-hal-common     (or `test`, which runs every suite)
# or directly in the container:
#   scripts/dev.py shell -C test-hal-common idf.py -B build_linux \
#       --preview set-target linux
#   scripts/dev.py shell -C test-hal-common idf.py -B build_linux build
#   ./test-hal-common/build_linux/esp32_sensors_hal_common_tests.elf
#
# The build directory is named after the target (`build_linux`), like the
# other subprojects'.

# Fail fast when building on the host instead of inside the dev container.
# The Dockerfile exports DOCKER_IMAGE=esp32_sensors (see the ENV at its end);
# requiring it keeps the toolchain reproducible and boxes the build/format/
# test workflow into the container documented in AGENTS.md.
if(NOT "$ENV{DOCKER_IMAGE}" STREQUAL "esp32_sensors")
    message(FATAL_ERROR
        "The test-hal-common host tests must be built inside the ESP-IDF dev "
        "container.\n"
        "DOCKER_IMAGE is not 'esp32_sensors', so this looks like a host "
        "invocation.\n"
        "Use:  scripts/dev.py test-hal-common")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Reusable components (`hal_*`, `logic`, `espidf_cpp`) live in the repo-root
# `components/` directory, which is not the default search path for this
# project (that would be `test-hal-common/components/`).
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../components")

# Restrict the component set to `main` (and its requirements) plus `hal_stub`.
# Without this the `linux` target also tries to build chip-specific components
# such as esp_driver_ana_cmpr, which have no linux implementation and fail to
# resolve. `hal_stub` is listed so the concept tests can name the concrete
# device types; it pulls in the header-only `hal_common` under test.
# `hal_board` is deliberately excluded (the two are mutually exclusive, and the
# GPIO-backed drivers have no `linux` target).
set(COMPONENTS main hal_stub)
set(EXCLUDE_COMPONENTS hal_board)

# The test ELF is named after this project:
# esp32_sensors_hal_common_tests.
project(esp32_sensors_hal_common_tests)
