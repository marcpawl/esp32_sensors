# GCC's static analyzer (`-fanalyzer`) for this repository's C++ code.
#
# The analyzer is deliberately not run over ESP-IDF or vendored components:
# their sources are not ours to fix, and the analyzer reports findings there
# that would fail the build under `-Werror` (for example, ESP-IDF's
# `wear_levelling` casting a `malloc()` of a flash sector size to `uint32_t*`).
# `third_party/` is excluded for the same reason as the format and clang-tidy
# gates: it is vendored code.
#
# Third-party *headers* included by our sources are outside a component's
# control, so the including source guards them with
# `#pragma GCC diagnostic ignored "-Wanalyzer-..."` around the `#include`
# instead.
#
# Call `thermo_enable_gcc_analyzer()` once after `project()`; the flags then go
# on every component target whose directory is in this repository. Pass explicit
# target names to cover non-component executables a component creates.

# `-fanalyzer` and `-Werror` are paired. Two warning categories come from
# libstdc++ internals: the analyzer cannot model `std::string`'s
# reserve()/reallocation or its `snprintf` model, so it reports leaks and
# reads of uninitialised storage inside libstdc++ headers and at the call
# sites that inline them. Both categories are suppressed locally, where the
# diagnostic is revealed, with the `// NOLINTBEGIN` diagnostic-pragma pattern
# used elsewhere in the tree. Every other analyzer warning fails the build.
set(_thermo_gcc_analyzer_options
    $<$<COMPILE_LANGUAGE:CXX>:-fanalyzer>
    $<$<COMPILE_LANGUAGE:CXX>:-Werror>
)

function(thermo_enable_gcc_analyzer)
    if(ARGC GREATER 0)
        foreach(target IN LISTS ARGN)
            target_compile_options(${target} PRIVATE
                ${_thermo_gcc_analyzer_options})
        endforeach()
        return()
    endif()

    idf_build_get_property(idf_path IDF_PATH)
    idf_build_get_property(build_components BUILD_COMPONENTS)
    foreach(component IN LISTS build_components)
        idf_component_get_property(component_dir ${component} COMPONENT_DIR)
        idf_component_get_property(component_lib ${component} COMPONENT_LIB)

        get_target_property(component_type ${component_lib} TYPE)
        if(component_type STREQUAL "INTERFACE_LIBRARY")
            continue()
        endif()

        # ESP-IDF components live under IDF_PATH; vendored code lives under
        # any `third_party/` directory. Everything else is ours.
        string(FIND "${component_dir}/" "${idf_path}/" idf_pos)
        string(FIND "${component_dir}/" "/third_party/" third_party_pos)
        if(idf_pos EQUAL 0 OR third_party_pos GREATER -1)
            continue()
        endif()

        target_compile_options(${component_lib} PRIVATE
            ${_thermo_gcc_analyzer_options})
    endforeach()
endfunction()
