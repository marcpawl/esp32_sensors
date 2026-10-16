cmake_minimum_required(VERSION 3.16)

# QEMU on-target test project for the ESP32 Solar-Powered Thermometer Logger.
#
# This is a standalone ESP-IDF project (independent of the top-level firmware
# project) whose `app_main` runs a Unity suite. It boots the ESP32-S3 image
# under the ESP-IDF QEMU fork (`idf.py qemu` -> `qemu-system-xtensa -M
# esp32s3`), so it exercises code that needs the real FreeRTOS scheduler and
# cannot run on the host `linux` target — chiefly `thermo::Task` — plus linkage
# checks for the pin-free `hal_stub` HAL devices.
#
# The ELF/app binary name derives from the project name, so this is what makes
# the QEMU artifact end in `_qemu` (esp32_sensors_qemu.bin).
#
# Build and run with:
#   scripts/dev.py shell -C qemu idf.py qemu
# or run the test (from inside the dev container; aborts on the host):
#   scripts/dev.py test-qemu

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Make the shared components visible to this standalone project. The QEMU build
# links `hal_stub` (the deterministic, pin-free HAL) and `espidf_cpp` (the
# FreeRTOS `Task`/queue wrappers); the production firmware links `hal_board`
# instead of `hal_stub`. Both HAL components expose the same `hal/*.hpp`
# interfaces.
list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../components")

# Trim the build to the minimal component set (main and its dependencies).
idf_build_set_property(MINIMAL_BUILD ON)

# This project links `hal_stub` (pins declared in software) instead of the
# GPIO-backed `hal_board`. Both expose `hal/switch.hpp` and define the same
# symbols, so exclude `hal_board` to keep exactly one implementation in view.
# Must be set before project().
set(EXCLUDE_COMPONENTS hal_board)

project(esp32_sensors_qemu)
