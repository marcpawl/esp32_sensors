cmake_minimum_required(VERSION 3.22)

# ESP-IDF scans each EXTRA_COMPONENT_DIRS entry one level deep and includes
# every CMakeLists.txt it finds in script mode to collect component
# requirements. This file is an ESP-IDF *project*, not a component: returning
# here keeps the sibling test projects from being parsed as components.
if(CMAKE_SCRIPT_MODE_FILE)
    return()
endif()


# On-target libespidfcpp tests for an ESP32-S3 development board.
#
# Build with:
#   scripts/dev.py build-test-espidf-cpp-esp32
#   scripts/dev.py shell -C components/espidfcpp/tests/test-espidf-cpp-esp32 \
#       idf.py -B build_esp32s3 build
# Flash and run with:
#   scripts/dev.py test-esp32 components/espidfcpp/tests/test-espidf-cpp-esp32
#
# The cases are the same `test-espidf-cpp-lib` component the linux project
# links; only the runner and the target-specific relay differ.

if(NOT EXISTS "/etc/esp32-sensors-dev-container")
    message(FATAL_ERROR
        "The test-espidf-cpp-esp32 tests must be built inside the ESP-IDF "
        "dev container.\nThe dev-container marker is missing, so this looks "
        "like a host invocation.")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# The shared cases live one level up in `tests/`; the component under test
# is in `components/espidfcpp/`, and its utility/HAL dependencies are
# grouped separately. ESP-IDF scans a component directory one level deep,
# so the two test *projects* under `tests/` are excluded below.
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../../.."
    "${CMAKE_CURRENT_LIST_DIR}/../../../utility"
    "${CMAKE_CURRENT_LIST_DIR}/../../../hal"
    "${CMAKE_CURRENT_LIST_DIR}/../.."
    "${CMAKE_CURRENT_LIST_DIR}/.."
)

set(COMPONENTS main test-espidf-cpp-lib)
# `tests/` also contains both project directories. Their top-level
# CMakeLists.txt files call project(), so they must not be parsed as
# components during requirement scanning.
set(EXCLUDE_COMPONENTS test-espidf-cpp-linux test-espidf-cpp-esp32)

include("${CMAKE_CURRENT_LIST_DIR}/../../../../cmake/gcc-analyzer.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../../../../cmake/compiler-warnings.cmake")

project(test-espidf-cpp-esp32)

thermo_enable_gcc_analyzer()
thermo_enable_compiler_warnings()
