# Undefined behaviour sanitizer (UBSan) for the host (`linux`) projects.
#
# Only this repository's own C++ components are instrumented: ESP-IDF and
# `third_party/` are not ours to fix, and a report from either would drown
# out a finding in our code. `thermo_own_component_targets()` applies the
# same filter used by the compiler-warning and GCC-analyzer modules.
#
# Every check is fatal. `-fno-sanitize-recover=all` makes the instrumented
# code abort on the first error instead of printing it and carrying on, and
# `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1` in docker-compose.yml
# gives the same behaviour to reports coming from code we did not build.
#
# Call `thermo_enable_ubsan()` once after `project()` in a linux project.
# Pass explicit target names for plain executables a component creates:
# those are not component libraries, so the no-argument form cannot reach
# them. Chip builds never get these flags (`CONFIG_IDF_TARGET_LINUX` is
# false there, and Xtensa GCC has no UBSan runtime to link).

option(THERMO_UBSAN
    "Enable undefined behaviour sanitizer in this repository's linux code" ON)

include("${CMAKE_CURRENT_LIST_DIR}/own-targets.cmake")

set(_thermo_ubsan_options
    $<$<COMPILE_LANGUAGE:CXX>:-fsanitize=undefined>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-sanitize-recover=all>
)

function(thermo_enable_ubsan)
    # GCC's static analyzer and UBSan instrumentation cannot share a build:
    # -fanalyzer misreads the checks inserted by -fsanitize=undefined as
    # uninitialised reads and fails with
    # -Werror=analyzer-use-of-uninitialized-value, even on untouched, correct
    # code. `scripts/dev.py build --analyzer` is a separate gate, so it runs
    # without the runtime instrumentation.
    if(NOT THERMO_UBSAN OR NOT CONFIG_IDF_TARGET_LINUX OR THERMO_GCC_ANALYZER)
        return()
    endif()

    if(ARGC GREATER 0)
        foreach(target IN LISTS ARGN)
            target_compile_options(${target} PRIVATE ${_thermo_ubsan_options})
            target_link_options(${target} PRIVATE -fsanitize=undefined)
        endforeach()
        return()
    endif()

    thermo_own_component_targets(targets)
    foreach(target IN LISTS targets)
        target_compile_options(${target} PRIVATE ${_thermo_ubsan_options})
    endforeach()

    # Component libraries are static archives: without the runtime on the
    # final ELF, their `__ubsan_handle_*` calls cannot be resolved.
    idf_build_get_property(elf EXECUTABLE)
    if(NOT elf)
        message(FATAL_ERROR "UBSan cannot find the project's executable target")
    endif()
    target_link_options(${elf} PRIVATE -fsanitize=undefined)
endfunction()
