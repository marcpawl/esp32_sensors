cmake_minimum_required(VERSION 3.22)

# Repository root — NOT an ESP-IDF project.
#
# The root holds the documentation, the shared `components/` HAL directory, the
# dev tooling (scripts/, Docker*, .devcontainer/, .idea/), and the subprojects.
# Each subproject is a standalone ESP-IDF project with its own `CMakeLists.txt`,
# `sdkconfig.defaults`, and build directory inside its own folder:
#
#   mcgowan/         ESP32-S3 firmware (hal_board)   -> mcgowan/build_esp32s3/mcgowan.bin
#   test/            host unit tests, `linux` target (hal_stub) -> test/build_linux/
#   espidf-cpp-test/ host tests for components/espidf_cpp, `linux`
#                                                -> espidf-cpp-test/build_linux/
#
# This file only aggregates them, so ONE build covers all of them. It is plain
# CMake (no repo-root sdkconfig): each target invokes `idf.py` for its
# subproject, and idf.py reads that subproject's committed defaults (target,
# Wi-Fi, HTTPS, ...) so nothing depends on IDF's ambient defaults.
#
# Build everything (container-only; see AGENTS.md):
#   scripts/dev.py build              # all subprojects
#   scripts/dev.py build mcgowan      # just the firmware
#
# or directly:
#   docker compose run --rm dev     cmake -B build_root && cmake --build build_root
#   docker compose run --rm dev     cmake -B build_root -DSUBPROJECTS="mcgowan" \
#                                   && cmake --build build_root
#
# Every target in this file is prefixed with its subproject name (e.g.
# mcgowan_build, test_build, espidf-cpp-test_build; a HAL component's would be
# hal_board_test) so it cannot collide with another target in this project,
# with names CMake / ESP-IDF define for themselves (test, build_test, ...), or
# with a subproject directory name.

if(NOT "$ENV{DOCKER_IMAGE}" STREQUAL "esp32_sensors")
    message(FATAL_ERROR
        "This project must be built inside the ESP-IDF dev container.\n"
        "DOCKER_IMAGE is not 'esp32_sensors', so this looks like a host "
        "invocation.\n"
        "Use:  scripts/dev.py build\n"
        "or:   docker compose run --rm dev cmake -B build_root")
endif()

if(NOT DEFINED ENV{IDF_PATH} OR NOT EXISTS "$ENV{IDF_PATH}/export.sh")
    message(FATAL_ERROR
        "IDF_PATH is not set; run this inside the ESP-IDF dev container.")
endif()

# Every idf.py invocation below goes through scripts/idf.sh, which establishes
# the ESP-IDF environment when the caller did not inherit it. That matters
# because this project is configured and built by CLion in the same container:
# the IDE's processes get the bare container environment (IDF_PATH and
# IDF_TOOLS_PATH, but none of the variables export.sh derives), and idf.py
# aborts with "TypeError: ... got 'NoneType'" on an unset ESP_IDF_VERSION
# before it touches the project. See scripts/idf.sh for the details.
set(IDF_RUNNER "${CMAKE_CURRENT_SOURCE_DIR}/scripts/idf.sh")

project(esp32_sensors_subprojects NONE)

# Which subprojects to build. Default: all of them.
set(SUBPROJECTS "mcgowan;test;espidf-cpp-test;donny" CACHE STRING
    "Subprojects to build: mcgowan, test, espidf-cpp-test, donny")
set_property(CACHE SUBPROJECTS PROPERTY STRINGS
    mcgowan test espidf-cpp-test donny)

set(KNOWN_SUBPROJECTS mcgowan test espidf-cpp-test donny)
foreach(sub IN LISTS SUBPROJECTS)
    if(NOT sub IN_LIST KNOWN_SUBPROJECTS)
        message(FATAL_ERROR
            "Unknown subproject '${sub}'. Known subprojects: "
            "${KNOWN_SUBPROJECTS}")
    endif()
endforeach()

# idf.py and its Python environment live in IDF_PATH/tools. Putting it on PATH
# here is a convenience for interactive use of this build tree; the targets do
# not depend on it, because scripts/idf.sh adds it (and the rest of the ESP-IDF
# environment) itself.
set(ENV{PATH} "$ENV{IDF_PATH}/tools:$ENV{PATH}")

# One `<subproject>_build` target per subproject. It runs idf.py in the
# subproject directory (`-C`) with the build tree inside that directory
# (`-B`); both are absolute so they resolve the same from the root and from
# inside the subproject directory. Each remaining argument is one idf.py
# invocation: a `|`-separated `SUBCOMMAND|arg|arg` string (e.g. "build" or
# "--preview|set-target|linux"), run in order. `|` (not `;`) separates the
# words because `;` is CMake's list separator; the invocations are `;`-separated
# in turn. One target per subproject keeps its name unique.
function(_idf_subproject sub build_dir)
    # idf.py is run through scripts/idf.sh rather than directly: the wrapper
    # guarantees the ESP-IDF environment, which an IDE-launched build does not
    # inherit (see the IDF_RUNNER comment above).
    set(common
        "${IDF_RUNNER}"
        idf.py
        -C "${CMAKE_CURRENT_SOURCE_DIR}/${sub}"
        -B "${CMAKE_CURRENT_SOURCE_DIR}/${sub}/${build_dir}"
        --no-hints)
    set(commands)
    foreach(step IN LISTS ARGN)
        string(REPLACE "|" ";" words "${step}")
        list(APPEND commands COMMAND ${common} ${words})
    endforeach()
    add_custom_target(${sub}_build ALL
        ${commands}
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${sub}"
        COMMENT "${sub}: build (${sub}/${build_dir})"
        VERBATIM
    )
endfunction()

# --- mcgowan: ESP32-S3 firmware (mcgowan.bin) -> target mcgowan_build ------
if(mcgowan IN_LIST SUBPROJECTS)
    _idf_subproject(mcgowan build_esp32s3 "build")
endif()

# --- test: host unit tests (linux target) -> target test_build ------------
# `set-target linux` is only meaningful when sdkconfig is (re)generated, so it
# is cheap on every invocation and keeps a fresh build tree pointed at linux.
if(test IN_LIST SUBPROJECTS)
    _idf_subproject(test build_linux "--preview|set-target|linux" "build")
endif()

# --- espidf-cpp-test: host tests for espidf_cpp (linux) -> *_build --------
# Same `linux` target rules as `test/`; built (not run) here, since running is
# `scripts/dev.py test-linux`'s job.
if(espidf-cpp-test IN_LIST SUBPROJECTS)
    _idf_subproject(espidf-cpp-test build_linux
        "--preview|set-target|linux" "build")
endif()

# --- donny: linux-emulated application (linux) -> target donny_build -------
# Same `linux` target rules as the host test projects.
if(donny IN_LIST SUBPROJECTS)
    _idf_subproject(donny build_linux "--preview|set-target|linux" "build")
endif()
