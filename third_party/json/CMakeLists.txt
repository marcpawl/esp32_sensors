# json: vendored third-party JSON library.
#
# nlohmann/json v3.11.3, a single-header, MIT-licensed C++ library
# (https://github.com/nlohmann/json). It is vendored rather than fetched so the
# build stays reproducible and offline; the upstream license is kept next to it
# (`LICENSE.MIT`) and the header is never edited, so it can be diffed against an
# upstream release.
#
# **Location: repo-root `third_party/`.** `third_party/` is deliberately outside
# every directory `scripts/dev.py` walks (`mcgowan`, `components`, `test`,
# `espidf-cpp-test`, `donny`, `scripts`), because those are clang-formatted and
# a vendored header must not be. It is also not any project's default component
# search path: a project opts in with
# `list(APPEND EXTRA_COMPONENT_DIRS .../third_party)` and then names `json` in
# its `REQUIRES`. Today only `donny` does.
#
# **Why the firmware does not use it.** The firmware formats its upload payload
# by hand (`logic/payload.hpp`) and has no JSON *parser* at all; adding a 900 KB
# header to a power- and flash-constrained ESP32-S3 binary for a diagnostic
# console would be wasteful. `donny` is a host-side emulation harness, where
# binary size does not matter and a full-featured parser keeps the console
# protocol honest.
#
# The library is header-only, so `SRCS` is empty and only `INCLUDE_DIRS` is
# needed. Note that `donny` builds with C++ exceptions and RTTI disabled
# (`CONFIG_COMPILER_CXX_EXCEPTIONS=n`, `CONFIG_COMPILER_CXX_RTTI=n`); the
# library works in that configuration, but it parses with
# `parse(..., nullptr, false)` + `is_discarded()` rather than relying on a
# `parse_error` exception being thrown, so a malformed line cannot abort the
# console task.

idf_component_register(
    INCLUDE_DIRS
        "include"
)
