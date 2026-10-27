#!/usr/bin/env bash
#
# Run a command (normally `idf.py`) with the ESP-IDF environment present,
# establishing that environment first if the caller did not inherit it.
#
# Why this exists
# ---------------
# The image's /opt/esp/entrypoint.sh sources $IDF_PATH/export.sh, but that only
# covers the container's *own* command. Processes started later by something
# that is not a login shell - most importantly the CMake/Ninja build that CLion
# spawns - inherit the bare container environment, which has IDF_PATH and
# IDF_TOOLS_PATH but none of the variables export.sh derives. idf.py then dies
# with
#
#   TypeError: expected string or bytes-like object, got 'NoneType'
#
# from idf_component_manager/idf_extensions.py, which does
# `Version.coerce(os.getenv('ESP_IDF_VERSION'))` - i.e. an unset
# ESP_IDF_VERSION blows up before any project work starts. `BASH_ENV` cannot
# paper over this: bash sources it only for *non-interactive* shells, and the
# image's ~/.bashrc returns early for non-interactive ones, so sourcing
# export.sh from there is a no-op (and /bin/sh ignores BASH_ENV entirely).
#
# So: establish the environment here, at the boundary, instead of relying on
# whatever the caller happened to inherit. Everything the aggregator CMake
# project runs goes through this script, which makes `cmake --build build_root`
# correct no matter how it was started (CLion, docker compose run, CI, a bare
# shell).
#
# Usage: scripts/idf.sh COMMAND [ARG]...
#
# Idempotent: when export.sh's variables are already present the command is
# executed directly (sourcing costs ~8 s, hence the guard).

set -eu

if [ "$#" -eq 0 ]; then
    echo "usage: $(basename "$0") COMMAND [ARG]..." >&2
    exit 2
fi

# idf.py lives in $IDF_PATH/tools; make sure it is reachable even when the rest
# of the environment came from the IDE (which sets PATH but not export.sh's
# derived variables).
if [ -n "${IDF_PATH-}" ]; then
    PATH="${IDF_PATH}/tools:${PATH}"
    export PATH
fi

# IDF_PYTHON_ENV_PATH and ESP_IDF_VERSION are set by export.sh and are required
# by idf.py; if either is missing the environment was not established, so
# establish it. stderr is left visible so a broken export.sh is diagnosable.
if [ -z "${IDF_PYTHON_ENV_PATH-}" ] || [ -z "${ESP_IDF_VERSION-}" ]; then
    if [ -z "${IDF_PATH-}" ] || [ ! -f "${IDF_PATH}/export.sh" ]; then
        echo "idf.sh: IDF_PATH is not set to an ESP-IDF checkout" >&2
        exit 1
    fi
    # shellcheck source=/dev/null
    . "${IDF_PATH}/export.sh" > /dev/null
fi

exec "$@"
