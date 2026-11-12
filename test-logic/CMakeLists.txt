# Run with:
#   scripts/dev.py test-logic          (or `test`, which runs every suite)
# or directly in the container:
#   scripts/dev.py shell -C test-logic idf.py -B build_linux \
#       --preview set-target linux
#   scripts/dev.py shell -C test-logic idf.py -B build_linux build
#   ./test-logic/build_linux/test-logic.elf
#
# The build directory is named after the target (`build_linux`), like the
# other subprojects'.
#
# See the "Testing approach" section of AGENTS.md.
cmake_minimum_required(VERSION 3.22)

# Fail fast when building on the host instead of inside the dev container.
# The Dockerfile exports DOCKER_IMAGE=esp32_sensors (see the ENV at its end);
# requiring it keeps the toolchain reproducible and boxes the build/format/
# test workflow into the container documented in AGENTS.md.
if(NOT "$ENV{DOCKER_IMAGE}" STREQUAL "esp32_sensors")
    message(FATAL_ERROR
        "The host tests must be built inside the ESP-IDF dev container.\n"
        "DOCKER_IMAGE is not 'esp32_sensors', so this looks like a host "
        "invocation.\n"
        "Use:  scripts/dev.py test")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Reusable components live in the repo-root `components/` (the ESP-IDF
# wrappers, the application logic and the sml wrapper) and
# `components/hal/` (the three HAL components). Neither is on this
# project's default search path (that would be `test-logic/components/`), and each needs its own entry: ESP-IDF
# scans a component directory one level deep, so a grouping directory is not
# descended into.
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../components"
    "${CMAKE_CURRENT_LIST_DIR}/../components/hal"
)

# Restrict the component set to `main` (and its requirements, which include the
# header-only `logic`) plus `hal_stub`.
# Without this the `linux` target also tries to build chip-specific components
# such as esp_driver_ana_cmpr, which have no linux implementation and fail to
# resolve. `hal_stub` is listed so the pin-free switch stub is built for the
# host; `hal_board` is deliberately excluded (the two are mutually exclusive,
# and the GPIO-backed switch has no `linux` target).
set(COMPONENTS main hal_stub logic)
set(EXCLUDE_COMPONENTS hal_board)

project(test-logic)
