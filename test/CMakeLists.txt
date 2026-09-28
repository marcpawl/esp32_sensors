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
#   set-target linux && idf.py build && ./build/esp32_sensors_tests.elf'
#
# See the "Testing approach" section of AGENTS.md.
cmake_minimum_required(VERSION 3.22)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Restrict the component set to `main` (and its requirements). Without this the
# `linux` target also tries to build chip-specific components such as
# esp_driver_ana_cmpr, which have no linux implementation and fail to resolve.
set(COMPONENTS main)

project(esp32_sensors_tests)
