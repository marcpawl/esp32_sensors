# AddressSanitizer (ASan) for the host (`linux`) projects.
#
# As with UBSan, only this repository's own C++ components are instrumented:
# ESP-IDF and `third_party/` keep their upstream flags, and the sanitizer
# reports from either would drown out findings in our code.
# `thermo_own_component_targets()` applies the same filter used by the
# compiler-warning, GCC-analyzer and UBSan modules.
#
# `-g` and `-fno-omit-frame-pointer` are pinned here instead of relying on
# IDF's linux debug defaults, so an ASan report always has symbols and a
# walkable stack. ASan is fatal by default; the runtime options in
# docker-compose.yml keep it that way (`halt_on_error=1`, `abort_on_error=1`)
# and print the stack.
#
# Call `thermo_enable_asan()` once after `project()` in a linux project.
# Pass explicit target names for plain executables a component creates:
# those are not component libraries, so the no-argument form cannot reach
# them. Chip builds get none of these flags. `build --analyzer` skips the
# instrumentation too: GCC's analyzer misreads sanitizer checks as
# uninitialised reads, so the two analysis modes stay separate gates.

option(THERMO_ASAN
    "Enable address sanitizer in this repository's linux code" ON)

include("${CMAKE_CURRENT_LIST_DIR}/own-targets.cmake")

set(_thermo_asan_options
    $<$<COMPILE_LANGUAGE:CXX>:-fsanitize=address>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-omit-frame-pointer>
    $<$<COMPILE_LANGUAGE:CXX>:-g>
)

function(thermo_enable_asan)
    # GCC's analyzer cannot share a translation unit with runtime sanitizer
    # instrumentation: with UBSan it reported uninitialised reads in code
    # that was correct. The `--analyzer` gate builds without either runtime,
    # and dev.py passes THERMO_GCC_ANALYZER explicitly to every project.
    if(NOT THERMO_ASAN OR NOT CONFIG_IDF_TARGET_LINUX OR THERMO_GCC_ANALYZER)
        return()
    endif()

    if(ARGC GREATER 0)
        foreach(target IN LISTS ARGN)
            target_compile_options(${target} PRIVATE ${_thermo_asan_options})
            target_link_options(${target} PRIVATE -fsanitize=address)
        endforeach()
        return()
    endif()

    thermo_own_component_targets(targets)
    foreach(target IN LISTS targets)
        target_compile_options(${target} PRIVATE ${_thermo_asan_options})
    endforeach()

    # Component libraries are static archives: the ELF needs the runtime.
    idf_build_get_property(elf EXECUTABLE)
    if(NOT elf)
        message(FATAL_ERROR "ASan cannot find the project's executable target")
    endif()
    target_link_options(${elf} PRIVATE -fsanitize=address)
endfunction()
