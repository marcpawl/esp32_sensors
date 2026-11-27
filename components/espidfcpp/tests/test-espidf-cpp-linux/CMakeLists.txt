cmake_minimum_required(VERSION 3.22)

# ESP-IDF scans each EXTRA_COMPONENT_DIRS entry one level deep and includes
# every CMakeLists.txt it finds in script mode to collect component
# requirements. This file is an ESP-IDF *project*, not a component: returning
# here keeps the sibling test projects from being parsed as components.
if(CMAKE_SCRIPT_MODE_FILE)
    return()
endif()


# The wrappers' host tests live in their own project so they build (and fail)
# independently of the firmware and of the ESP32-S3 wrapper tests.
#
# Build and run with:
#   scripts/dev.py test-linux components/espidfcpp/tests/test-espidf-cpp-linux
#   scripts/dev.py build-test-espidf-cpp-linux
# or directly in the container:
#   scripts/dev.py shell -C components/espidfcpp/tests/test-espidf-cpp-linux \
#       idf.py -B build_linux --preview set-target linux
#   scripts/dev.py shell -C components/espidfcpp/tests/test-espidf-cpp-linux \
#       idf.py -B build_linux build
#   ./components/espidfcpp/tests/test-espidf-cpp-linux/build_linux/\
#       test-espidf-cpp-linux.elf

if(NOT EXISTS "/etc/esp32-sensors-dev-container")
    message(FATAL_ERROR
        "The test-espidf-cpp-linux host tests must be built inside the "
        "ESP-IDF dev container.\n"
        "The dev-container marker is missing, so this looks like a host "
        "invocation.\n"
        "Use:  scripts/dev.py test-linux "
        "components/espidfcpp/tests/test-espidf-cpp-linux")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# The reusable components live in the repo-root `components/` directory (the
# sml wrapper). The component under test lives in `components/espidfcpp/`, the
# HAL components in `components/hal/`, and the shared cases one level below
# this project in `tests/`. ESP-IDF scans a component directory one level
# deep, so the two test *projects* under `tests/` are excluded below.
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../../.."
    "${CMAKE_CURRENT_LIST_DIR}/../../../utility"
    "${CMAKE_CURRENT_LIST_DIR}/../../../hal"
    "${CMAKE_CURRENT_LIST_DIR}/../.."
    "${CMAKE_CURRENT_LIST_DIR}/.."
)

# `main` supplies the host runner and the linux-only cases; the shared
# component
# supplies the target-neutral cases; `hal_stub` supplies the GPIO mock that
# libespidfcpp privately requires on this target. Restricting the set keeps the
# linux build from trying chip-only components such as esp_driver_ana_cmpr.
set(COMPONENTS main test-espidf-cpp-lib hal_stub)
# `tests/` also contains both project directories. Their top-level
# CMakeLists.txt files call project(), so they must not be parsed as
# components during requirement scanning.
set(EXCLUDE_COMPONENTS
    test-espidf-cpp-linux
    test-espidf-cpp-esp32
    hal_board
)

include("${CMAKE_CURRENT_LIST_DIR}/../../../../cmake/gcc-analyzer.cmake")

# UBSan instruments our linux C++ code and aborts on the first finding; the
# module explains the target guard and why vendored code is excluded.
include("${CMAKE_CURRENT_LIST_DIR}/../../../../cmake/ubsan.cmake")

# ASan adds heap and stack checks with pinned debug symbols; the module
# explains the target guard and the runtime options.
include("${CMAKE_CURRENT_LIST_DIR}/../../../../cmake/asan.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../../../../cmake/compiler-warnings.cmake")

project(test-espidf-cpp-linux)

thermo_enable_gcc_analyzer()
thermo_enable_compiler_warnings()
thermo_enable_ubsan()
thermo_enable_asan()
