cmake_minimum_required(VERSION 3.22)

# On-target `esp_timer` test executable for an ESP32-S3 development board.
#
# Build and flash with:
#   scripts/dev.py shell -C components/hal/tests/esp_timer/board \
#       idf.py -B build_esp32s3 build
#   scripts/dev.py shell -C components/hal/tests/esp_timer/board \
#       idf.py -B build_esp32s3 -p $ESP32_PORT flash monitor
#
# The test cases themselves are the `esp_timer_tests` component shared with the
# `linux` stub executable next to this project.

if(NOT EXISTS "/etc/esp32-sensors-dev-container")
    message(FATAL_ERROR
        "The esp_timer board tests must be built inside the ESP-IDF dev "
        "container.\nThe dev-container marker is missing, so this looks like "
        "a host invocation.")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# The shared test-case library lives in `../components`, which is not on the
# default component search path (`<project>/components`).
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../components")

# Restrict the component set to `main` and the components it requires. Left
# unrestricted, the build also pulls in every default IDF component (Wi-Fi,
# Bluetooth, the display stack, ...), which this test never touches: it costs
# most of the build time and makes ESP-IDF's own component validation report
# warnings about its `esp_wifi` / `wpa_supplicant` include directories.
set(COMPONENTS main esp_timer_tests)

# GCC's static analyzer for this repository's C++ code only; the module
# explains why ESP-IDF and vendored components are excluded.
include("${CMAKE_CURRENT_LIST_DIR}/../../../../../cmake/gcc-analyzer.cmake")

# Warnings are errors in this repository's own code; the module
# explains the ESP-IDF defaults it overrides.
include("${CMAKE_CURRENT_LIST_DIR}/../../../../../cmake/compiler-warnings.cmake")

project(esp32_sensors_esp_timer_board_tests)

# `BUILD_COMPONENTS` is populated by project(); both flag helpers go on
# this repository's component targets only.
thermo_enable_gcc_analyzer()
thermo_enable_compiler_warnings()
