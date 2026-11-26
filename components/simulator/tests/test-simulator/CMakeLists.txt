cmake_minimum_required(VERSION 3.22)

# The simulator's tests live in their own project so they build (and fail)
# independently of every other suite.
#
# Build and run with:
#   scripts/dev.py test-simulator     (or `test`, which runs every suite)
# or directly in the container:
#   scripts/dev.py shell -C components/simulator/tests/test-simulator \
#       idf.py -B build_linux --preview set-target linux
#   scripts/dev.py shell -C components/simulator/tests/test-simulator \
#       idf.py -B build_linux build
#   ./components/simulator/tests/test-simulator/build_linux/test-simulator.elf
#
# The build directory is named after the target (`build_linux`), like the
# other subprojects'.

# Fail fast when building on the host instead of inside the dev container.
# The dev image creates /etc/esp32-sensors-dev-container, and
# docker-compose.yml mounts it for the pinned image; requiring it keeps the
# toolchain reproducible and boxes the build/test workflow into the
# container documented in AGENTS.md.
if(NOT EXISTS "/etc/esp32-sensors-dev-container")
    message(FATAL_ERROR
        "The test-simulator host tests must be built inside the ESP-IDF dev "
        "container.\n"
        "The dev-container marker is missing, so this looks like a host "
        "invocation.\n"
        "Use:  scripts/dev.py test-simulator")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Reusable components live in the repo-root `components/` (the sml wrapper) and
# the simulator's own components in `components/simulator/`. Neither is on this
# project's default search path (that would be
# `components/simulator/tests/test-simulator/components/`), and each needs its
# own entry: ESP-IDF scans a component directory one level deep, so a grouping
# directory is not descended into.
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../../.."
    "${CMAKE_CURRENT_LIST_DIR}/../../../utility"
    "${CMAKE_CURRENT_LIST_DIR}/../.."
)

# Restrict the component set to `main` and the simulator component under test.
# Without this the `linux` target also tries to build chip-specific components
# such as esp_driver_ana_cmpr, which have no linux implementation and fail to
# resolve. `hal_board` is deliberately excluded (the two HAL implementations
# are mutually exclusive, and the GPIO-backed drivers have no `linux` target).
set(COMPONENTS main libsimulator)
set(EXCLUDE_COMPONENTS hal_board)

# GCC's static analyzer for this repository's C++ code only; the module
# explains why ESP-IDF and vendored components are excluded.
include("${CMAKE_CURRENT_LIST_DIR}/../../../../cmake/gcc-analyzer.cmake")

# UBSan instruments our linux C++ code and aborts on the first finding; the
# module explains the target guard and why vendored code is excluded.
include("${CMAKE_CURRENT_LIST_DIR}/../../../../cmake/ubsan.cmake")

# ASan adds heap and stack checks with pinned debug symbols; the module
# explains the target guard and the runtime options.
include("${CMAKE_CURRENT_LIST_DIR}/../../../../cmake/asan.cmake")

# Warnings are errors in this repository's own code; the module
# explains the ESP-IDF defaults it overrides.
include("${CMAKE_CURRENT_LIST_DIR}/../../../../cmake/compiler-warnings.cmake")

# The test ELF is named after this project: test-simulator.
project(test-simulator)

# `BUILD_COMPONENTS` is populated by project(); all four flag helpers go
# on this repository's component targets only.
thermo_enable_gcc_analyzer()
thermo_enable_compiler_warnings()
thermo_enable_ubsan()
thermo_enable_asan()
