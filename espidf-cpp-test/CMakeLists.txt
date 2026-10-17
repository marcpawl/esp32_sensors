cmake_minimum_required(VERSION 3.22)

# `espidf-cpp-test` — host unit tests for the `espidf_cpp` component.
#
# This is a standalone ESP-IDF project that targets the `linux` target only
# (never a chip): it builds the Unity test runner for the wrappers in
# `components/espidf_cpp` and runs natively inside the dev container, so no
# ESP32 hardware is needed. Keeping the wrappers' tests in their own project
# means they build (and fail) independently of the firmware's tests.
#
# Build and run with:
#   scripts/dev.py test-linux
# or directly in the container:
#   scripts/dev.py shell -C espidf-cpp-test idf.py -B build_linux \
#       --preview set-target linux
#   scripts/dev.py shell -C espidf-cpp-test idf.py -B build_linux build
#   ./espidf-cpp-test/build_linux/esp32_sensors_espidf_cpp_tests.elf
#
# The build directory is named after the target (`build_linux`), like the
# other subprojects'.

# Fail fast when building on the host instead of inside the dev container.
# The Dockerfile exports DOCKER_IMAGE=esp32_sensors (see the ENV at its end);
# requiring it keeps the toolchain reproducible and boxes the build/test
# workflow into the container documented in AGENTS.md.
if(NOT "$ENV{DOCKER_IMAGE}" STREQUAL "esp32_sensors")
    message(FATAL_ERROR
        "The espidf-cpp-test host tests must be built inside the ESP-IDF dev "
        "container.\n"
        "DOCKER_IMAGE is not 'esp32_sensors', so this looks like a host "
        "invocation.\n"
        "Use:  scripts/dev.py test-linux")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# The component under test lives in the repo-root `components/` directory,
# which is not on this project's default search path (that would be
# `espidf-cpp-test/components/`).
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../components")

# Restrict the component set to `main` (and its requirements, which pull in
# `espidf_cpp`) plus `hal_stub`. Without this the `linux` target also tries to
# build chip-specific components such as esp_driver_ana_cmpr, which have no
# linux implementation and fail to resolve. `hal_stub` is listed to keep the
# component set identical to `test/`'s and available to future tests;
# `hal_board` is deliberately excluded (the two are mutually exclusive, and the
# GPIO-backed drivers have no `linux` target).
set(COMPONENTS main hal_stub)
set(EXCLUDE_COMPONENTS hal_board)

# The test ELF is named after this project: esp32_sensors_espidf_cpp_tests.
project(esp32_sensors_espidf_cpp_tests)
