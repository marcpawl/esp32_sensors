# Host-based unit tests for the ESP32 Solar-Powered Thermometer Logger.
#
# These tests build for the ESP-IDF `linux` target and run natively on the host
# (inside the dev container), so no ESP32 hardware is required. Only the
# ESP-IDF-free logic modules are exercised here — payload formatting (§9),
# temperature serialization (§9.3), configuration validation (§5.1), the RTC
# sample buffer (§4.2), and ROM-ID/CRC helpers (§5.1.3).
#
# Run with:
#   docker compose run --rm dev bash -lc 'cd test && idf.py --preview
#   -B build_linux set-target linux && idf.py -B build_linux build &&
#   ./build_linux/esp32_sensors_tests.elf'
# (or simply `scripts/dev.py test`). The build directory is named after the
# target (`build_linux`) so it never clashes with the firmware `build_esp32s3`.
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

# Restrict the component set to `main` (and its requirements). Without this the
# `linux` target also tries to build chip-specific components such as
# esp_driver_ana_cmpr, which have no linux implementation and fail to resolve.
set(COMPONENTS main)

project(esp32_sensors_tests)
