cmake_minimum_required(VERSION 3.16)

# Fail fast when building on the host instead of inside the dev container.
# The Dockerfile exports DOCKER_IMAGE=esp32_sensors (see the ENV at its end);
# requiring it keeps the toolchain reproducible and boxes the build/format/
# test workflow into the container documented in AGENTS.md.
if(NOT "$ENV{DOCKER_IMAGE}" STREQUAL "esp32_sensors")
    message(FATAL_ERROR
        "This project must be built inside the ESP-IDF dev container.\n"
        "DOCKER_IMAGE is not 'esp32_sensors', so this looks like a host "
        "invocation.\n"
        "Use:  scripts/dev.py build\n"
        "or:   docker compose run --rm dev idf.py -B build_esp32s3 build")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32_sensors)
