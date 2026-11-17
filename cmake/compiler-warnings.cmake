# Warnings are errors in this repository's own code.
#
# ESP-IDF compiles every component with `-Wall -Wextra -Werror`, but its
# defaults then downgrade five categories back to warnings and switch a few
# others off (`tools/cmake/build.cmake`, in
# `__build_set_default_build_specifications`). That is the right policy for
# ESP-IDF and its dependencies, whose sources are not ours to fix.
#
# This module re-enables the warnings as errors on this repository's targets,
# so the categories ESP-IDF tolerates fail our builds:
#
#   * `unused-function`, `unused-variable`, `unused-but-set-variable` — dead
#     code in our sources is a mistake, not a style choice;
#   * `deprecated-declarations` — calling a deprecated ESP-IDF API is a signal
#     to migrate before the API is removed;
#   * everything else in `-Wextra`.
#
# A specific `-Wno-error=<warning>` overrides a later plain `-Werror`, so each
# downgraded category is named again with `-Werror=<warning>`; the later,
# equally specific option wins. ESP-IDF's `-Wno-unused-parameter`,
# `-Wno-sign-compare` and `-Wno-enum-conversion` are re-applied after the
# `-Werror` options: clang's `-Werror=<group>` re-enables every warning in the
# group (GCC's does not), which would raise diagnostics inside ESP-IDF headers
# that clang-tidy then reports as errors. This module makes the warnings
# ESP-IDF emits fatal; it does not add new ones. It also suppresses
# `-Wattributes`, because `[[thermo::isr_safe]]` is a deliberate
# documentation-only annotation (see `utility/isr_safe.hpp`) that neither
# compiler understands.
#
# Include this file and call `thermo_enable_compiler_warnings()` after
# `project()`. Pass explicit target names for plain executables the component
# loop cannot reach.
include("${CMAKE_CURRENT_LIST_DIR}/own-targets.cmake")

set(_thermo_compiler_warning_options
    -Wall
    -Wextra
    -Werror
    -Werror=unused-function
    -Werror=unused-variable
    -Werror=unused-but-set-variable
    -Werror=deprecated-declarations
    -Werror=extra
    # Clang's `-Werror=<group>` re-enables every warning in the group, unlike
    # GCC's, so ESP-IDF's switched-off warnings are restored explicitly for
    # both compilers: this module must not add diagnostics for code we do not
    # own. `SHELL:` keeps CMake from de-duplicating these against ESP-IDF's
    # identical earlier options, which would leave the flag before
    # `-Werror=extra` and let clang switch the warning back on.
    "SHELL:-Wno-unused-parameter"
    "SHELL:-Wno-sign-compare"
    "SHELL:-Wno-enum-conversion"
    # `[[thermo::isr_safe]]` (utility/isr_safe.hpp) is a documentation-only
    # annotation, so both compilers would otherwise report it as an unknown
    # attribute. This suppression is deliberate and limited to our targets.
    -Wno-attributes
)

function(thermo_enable_compiler_warnings)
    if(ARGC GREATER 0)
        foreach(target IN LISTS ARGN)
            target_compile_options(${target} PRIVATE
                ${_thermo_compiler_warning_options})
        endforeach()
        return()
    endif()

    thermo_own_component_targets(targets)
    foreach(target IN LISTS targets)
        target_compile_options(${target} PRIVATE
            ${_thermo_compiler_warning_options})
    endforeach()
endfunction()
