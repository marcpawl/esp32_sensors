cmake_minimum_required(VERSION 3.22)

# The simulator's tests live in their own project so they build (and fail)
# independently of every other suite.
#
# Build and run with:
#   scripts/dev.py test-simulator     (or `test`, which runs every suite)
# or directly in the container:
#   scripts/dev.py shell -C components/simulator/tests/test-simulator \
#       idf.py -B build_linux --preview set-target linux
#   scripts/dev.py shell -C components/simulator/tests/test-simulator \
#       idf.py -B build_linux build
#   ./components/simulator/tests/test-simulator/build_linux/test-simulator.elf
#
# The build directory is named after the target (`build_linux`), like the
# other subprojects'.

# Fail fast when building on the host instead of inside the dev container.
# The Dockerfile exports DOCKER_IMAGE=esp32_sensors (see the ENV at its end);
# requiring it keeps the toolchain reproducible and boxes the build/test
# workflow into the container documented in AGENTS.md.
if(NOT "$ENV{DOCKER_IMAGE}" STREQUAL "esp32_sensors")
    message(FATAL_ERROR
        "The test-simulator host tests must be built inside the ESP-IDF dev "
        "container.\n"
        "DOCKER_IMAGE is not 'esp32_sensors', so this looks like a host "
        "invocation.\n"
        "Use:  scripts/dev.py test-simulator")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Reusable components live in the repo-root `components/` (the sml wrapper) and
# the simulator's own components in `components/simulator/`. Neither is on this
# project's default search path (that would be
# `components/simulator/tests/test-simulator/components/`), and each needs its
# own entry: ESP-IDF scans a component directory one level deep, so a grouping
# directory is not descended into.
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../../.."
    "${CMAKE_CURRENT_LIST_DIR}/../.."
)

# Restrict the component set to `main` and the simulator component under test.
# Without this the `linux` target also tries to build chip-specific components
# such as esp_driver_ana_cmpr, which have no linux implementation and fail to
# resolve. `hal_board` is deliberately excluded (the two HAL implementations
# are mutually exclusive, and the GPIO-backed drivers have no `linux` target).
set(COMPONENTS main libsimulator)
set(EXCLUDE_COMPONENTS hal_board)

# The test ELF is named after this project: test-simulator.
project(test-simulator)
