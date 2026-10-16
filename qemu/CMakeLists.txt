cmake_minimum_required(VERSION 3.16)

# Native (linux-target) test project for the ESP32 Solar-Powered Thermometer
# Logger.
#
# This is a standalone ESP-IDF project (independent of the top-level firmware
# project) whose `app_main` runs a Unity suite. It builds for the ESP-IDF
# `linux` target and runs as a native ELF, so it exercises code that needs a
# real FreeRTOS scheduler (not just stubs) — chiefly `thermo::Task` — plus
# linkage checks for the pin-free `hal_stub` HAL devices. Running on the linux
# target keeps it hardware-independent while still using ESP-IDF's FreeRTOS
# (pthread) port, so no chip or emulator is required.
#
# The ELF/app binary name derives from the project name, so this is what makes
# the artifact end in `_qemu` (esp32_sensors_qemu.elf).
#
# Build and run with:
#   scripts/dev.py test-qemu

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Make the shared components visible to this standalone project. This project
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
