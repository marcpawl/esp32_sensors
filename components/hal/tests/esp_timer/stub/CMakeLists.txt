cmake_minimum_required(VERSION 3.22)

# Host (`linux`) `esp_timer` test executable. It links `hal_stub`, which
# implements the `esp_timer` C API on top of FreeRTOS software timers, and the
# same `esp_timer_tests` component the ESP32-S3 project uses.
#
# Build and run with:
#   scripts/dev.py shell -C components/hal/tests/esp_timer/stub \
#       idf.py -B build_linux --preview set-target linux
#   scripts/dev.py shell -C components/hal/tests/esp_timer/stub \
#       idf.py -B build_linux build
#   ./components/hal/tests/esp_timer/stub/build_linux/\
#       esp32_sensors_esp_timer_stub_tests.elf

if(NOT "$ENV{DOCKER_IMAGE}" STREQUAL "esp32_sensors")
    message(FATAL_ERROR
        "The esp_timer host tests must be built inside the ESP-IDF dev "
        "container.\nDOCKER_IMAGE is not 'esp32_sensors', so this looks like "
        "a host invocation.")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Reusable components: `components/` (the grouping root), `components/hal/`
# (the HAL components, including `hal_stub`), and `../components` (the shared
# `esp_timer_tests`). ESP-IDF scans a component directory one level deep, so
# each grouping directory needs its own entry.
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../../../.."
    "${CMAKE_CURRENT_LIST_DIR}/../../.."
    "${CMAKE_CURRENT_LIST_DIR}/../components"
)

# `hal_stub` supplies the esp_timer symbols on this target and `esp_timer_tests`
# the cases. The linux target must not try to build the chip-only `hal_board`.
set(COMPONENTS main esp_timer_tests hal_stub)
set(EXCLUDE_COMPONENTS hal_board)

project(esp32_sensors_esp_timer_stub_tests)
