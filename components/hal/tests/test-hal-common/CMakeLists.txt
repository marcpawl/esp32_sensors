cmake_minimum_required(VERSION 3.22)

# The concepts name concrete device types through each implementation's public
# header (`hal_stub/led.hpp`, `hal_stub/wifi.hpp`, ...), so this project links the
# pin-free `hal_stub` implementation — the only one with a `linux` target.
#
# Build and run with:
#   scripts/dev.py test-hal-common     (or `test`, which runs every suite)
# or directly in the container:
#   scripts/dev.py shell -C components/hal/tests/test-hal-common idf.py -B build_linux \
#       --preview set-target linux
#   scripts/dev.py shell -C components/hal/tests/test-hal-common idf.py -B build_linux build
#   ./components/hal/tests/test-hal-common/build_linux/esp32_sensors_hal_common_tests.elf
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

# Reusable components live in the repo-root `components/` (the ESP-IDF
# wrappers and the sml wrapper) and `components/hal/` (the three HAL
# components). Neither is on this project's default search path (that would be
# `components/hal/tests/test-hal-common/components/`), and each needs its own
# entry: ESP-IDF scans a component directory one level deep, so a grouping
# directory is not descended into.
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../../.."
    "${CMAKE_CURRENT_LIST_DIR}/../.."
)

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
