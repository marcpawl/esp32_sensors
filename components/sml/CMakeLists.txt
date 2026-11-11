# sml: ESP-IDF component wrapper around the vendored boost-ext/sml submodule.
#
# The library itself is NOT in this directory. It is a git submodule at
# `third_party/sml`, pinned to tag **v1.2.0** (commit 2d8485d) and recorded in
# `.gitmodules`. This directory only teaches the ESP-IDF build about it: the
# upstream tree is header-only and ships its own `CMakeLists.txt` full of
# upstream tests and examples, so it must not be treated as a component itself
# — the wrapper keeps `idf_component_register` out of the vendored tree,
# exactly like `third_party/json` keeps its own component file next to the
# header.
#
# **Why a submodule rather than a copied tree.** The library is used by the
# shared `logic` component (state machines for the operating modes and the
# run-mode cycle). Pinning a released tag keeps the build reproducible and lets
# `git submodule update` move it deliberately, without the byte-for-byte
# "vendored" discipline `third_party/json` needs.
#
# **The include directory points *outside* this component.** ESP-IDF resolves
# INCLUDE_DIRS relative to the component's CMakeLists.txt and accepts any
# existing directory, so `../../third_party/sml/include` is fine. Keeping the
# library at `third_party/sml` (rather than copying it under `components/`)
# puts it outside every directory `scripts/dev.py` clang-formats (`ROOTS`) and
# is excluded from clang-tidy as vendored code, so the pre-commit hook never
# checks it — the header stays byte-identical to upstream.
#
# **The submodule must be checked out first.** A bare clone leaves
# `third_party/sml/` empty, and an empty include directory is a hard configure
# error in ESP-IDF; fail here instead with an actionable message. `scripts/dev.py
# build` (and `test`, `clang-tidy`) fetch it automatically, and
# `scripts/dev.py submodules` does so explicitly.
#
# Consumers name the component `sml` in REQUIRES and include it as
# `<boost/sml.hpp>`, the upstream include path. The library is header-only, so
# `SRCS` is empty and only `INCLUDE_DIRS` is needed.

set(sml_include_dir
    "${CMAKE_CURRENT_LIST_DIR}/../../third_party/sml/include")

if(NOT EXISTS "${sml_include_dir}/boost/sml.hpp")
    message(FATAL_ERROR
        "The boost-ext/sml submodule is not checked out "
        "(missing ${sml_include_dir}/boost/sml.hpp).\n"
        "Fetch it with:  scripts/dev.py submodules\n"
        "or:             git submodule update --init --recursive")
endif()

idf_component_register(
    INCLUDE_DIRS
        "${sml_include_dir}"
)
