cmake_minimum_required(VERSION 3.22)

# The containers' tests live in their own project so they build (and fail)
# independently of the components that use them.
#
# Build and run with:
#   scripts/dev.py test-utility     (or `test`, which runs every suite)
# or directly in the container:
#   scripts/dev.py shell -C components/utility/tests/test-utility \
#       idf.py -B build_linux --preview set-target linux
#   scripts/dev.py shell -C components/utility/tests/test-utility \
#       idf.py -B build_linux build
#   ./components/utility/tests/test-utility/build_linux/test-utility.elf
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
        "The test-utility host tests must be built inside the ESP-IDF dev "
        "container.\n"
        "The dev-container marker is missing, so this looks like a host "
        "invocation.\n"
        "Use:  scripts/dev.py test-utility")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# The component under test lives in `components/utility/`, which is not on this
# project's default search path (that would be
# `components/utility/tests/test-utility/components/`). ESP-IDF scans a
# component directory one level deep, so the grouping directory needs its own
# entry.
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../../.."
    "${CMAKE_CURRENT_LIST_DIR}/../.."
)

# Restrict the component set to `main` (and its requirements, which pull in
# `libutility`). Without this the `linux` target also tries to build
# chip-specific components such as esp_driver_ana_cmpr, which have no linux
# implementation and fail to resolve.
set(COMPONENTS main libutility)
set(EXCLUDE_COMPONENTS hal_board)

# GCC's static analyzer for this repository's C++ code only; the module
# explains why ESP-IDF and vendored components are excluded.
include("${CMAKE_CURRENT_LIST_DIR}/../../../../cmake/gcc-analyzer.cmake")

# Warnings are errors in this repository's own code; the module
# explains the ESP-IDF defaults it overrides.
include("${CMAKE_CURRENT_LIST_DIR}/../../../../cmake/compiler-warnings.cmake")

# The test ELF is named after this project: test-utility.
project(test-utility)

# `BUILD_COMPONENTS` is populated by project(); both flag helpers go on
# this repository's component targets only.
thermo_enable_gcc_analyzer()
thermo_enable_compiler_warnings()
