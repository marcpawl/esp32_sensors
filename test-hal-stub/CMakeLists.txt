cmake_minimum_required(VERSION 3.22)

# `test-hal-stub` — host unit tests for the pin-free `hal_stub` devices.
#
# This is a standalone ESP-IDF project that targets the `linux` target only
# (never a chip): it builds the Unity test runner for the software-driven
# implementations in `components/hal_stub` (LED, push button, 1-Wire bus, chip
# temperature, WiFi radio, one-shot timer, and the aggregate `BoardStub`) and
# runs natively inside the dev container, so no ESP32 hardware is needed.
#
# The shared vocabulary those devices implement (`hal_common`) has its own
# project, `test-hal-common/`; the pure application logic and the firmware
# sources are covered by `test/`.
#
# Build and run with:
#   scripts/dev.py test-hal-stub       (or `test`, which runs every suite)
# or directly in the container:
#   scripts/dev.py shell -C test-hal-stub idf.py -B build_linux \
#       --preview set-target linux
#   scripts/dev.py shell -C test-hal-stub idf.py -B build_linux build
#   ./test-hal-stub/build_linux/esp32_sensors_hal_stub_tests.elf
#
# The build directory is named after the target (`build_linux`), like the
# other subprojects'.

# Fail fast when building on the host instead of inside the dev container.
# The Dockerfile exports DOCKER_IMAGE=esp32_sensors (see the ENV at its end);
# requiring it keeps the toolchain reproducible and boxes the build/format/
# test workflow into the container documented in AGENTS.md.
if(NOT "$ENV{DOCKER_IMAGE}" STREQUAL "esp32_sensors")
    message(FATAL_ERROR
        "The test-hal-stub host tests must be built inside the ESP-IDF dev "
        "container.\n"
        "DOCKER_IMAGE is not 'esp32_sensors', so this looks like a host "
        "invocation.\n"
        "Use:  scripts/dev.py test-hal-stub")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Reusable components (`hal_*`, `logic`, `espidf_cpp`) live in the repo-root
# `components/` directory, which is not the default search path for this
# project (that would be `test-hal-stub/components/`).
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../components")

# Restrict the component set to `main` (and its requirements) plus `hal_stub`.
# Without this the `linux` target also tries to build chip-specific components
# such as esp_driver_ana_cmpr, which have no linux implementation and fail to
# resolve. `hal_stub` pulls in the header-only `hal_common` it implements.
# `hal_board` is deliberately excluded (the two are mutually exclusive, and the
# GPIO-backed drivers have no `linux` target).
set(COMPONENTS main hal_stub)
set(EXCLUDE_COMPONENTS hal_board)

# The test ELF is named after this project: esp32_sensors_hal_stub_tests.
project(esp32_sensors_hal_stub_tests)
