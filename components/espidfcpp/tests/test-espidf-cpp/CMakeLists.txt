cmake_minimum_required(VERSION 3.22)

# The wrappers' tests live in their own project so they build (and fail)
# independently of the firmware's tests.
#
# Build and run with:
#   scripts/dev.py test-espidf-cpp     (or `test`, which runs every suite)
# or directly in the container:
#   scripts/dev.py shell -C components/espidfcpp/tests/test-espidf-cpp \
#       idf.py -B build_linux --preview set-target linux
#   scripts/dev.py shell -C components/espidfcpp/tests/test-espidf-cpp \
#       idf.py -B build_linux build
#   ./components/espidfcpp/tests/test-espidf-cpp/build_linux/test-espidf-cpp.elf
#
# The build directory is named after the target (`build_linux`), like the
# other subprojects'.

# Fail fast when building on the host instead of inside the dev container.
# The Dockerfile exports DOCKER_IMAGE=esp32_sensors (see the ENV at its end);
# requiring it keeps the toolchain reproducible and boxes the build/test
# workflow into the container documented in AGENTS.md.
if(NOT "$ENV{DOCKER_IMAGE}" STREQUAL "esp32_sensors")
    message(FATAL_ERROR
        "The test-espidf-cpp host tests must be built inside the ESP-IDF dev "
        "container.\n"
        "DOCKER_IMAGE is not 'esp32_sensors', so this looks like a host "
        "invocation.\n"
        "Use:  scripts/dev.py test-espidf-cpp")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# The reusable components live in the repo-root `components/` directory (the
# application logic and the sml wrapper). The component under test lives in
# `components/espidfcpp/` and the HAL components in `components/hal/`; each
# grouping directory needs its own entry: ESP-IDF scans a component directory
# one level deep, so a grouping directory is not descended into.
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../../.."
    "${CMAKE_CURRENT_LIST_DIR}/../../../hal"
    "${CMAKE_CURRENT_LIST_DIR}/../.."
)

# Restrict the component set to `main` (and its requirements, which pull in
# `libespidfcpp`) plus `hal_stub`. Without this the `linux` target also tries to
# build chip-specific components such as esp_driver_ana_cmpr, which have no
# linux implementation and fail to resolve. `hal_stub` is listed to keep the
# component set identical to `test/`'s and available to future tests;
# `hal_board` is deliberately excluded (the two are mutually exclusive, and the
# GPIO-backed drivers have no `linux` target).
set(COMPONENTS main hal_stub)
set(EXCLUDE_COMPONENTS hal_board)

# The test ELF is named after this project: test-espidf-cpp.
project(test-espidf-cpp)
