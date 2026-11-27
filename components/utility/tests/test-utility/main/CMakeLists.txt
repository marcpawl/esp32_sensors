# `libutility` (the component under test) is a normal requirement. WHOLE_ARCHIVE
# is required so the TEST_CASE registrations in the test sources are linked into
# the final executable even though nothing references them.

set(test_srcs
    "test_main.cpp"
    "test_static_vector.cpp"
    "test_static_string.cpp"
    "test_sequence_lock.cpp"
)

idf_component_register(
    SRCS ${test_srcs}
    INCLUDE_DIRS
        "."
    REQUIRES
        libutility
    PRIV_REQUIRES
        unity
    WHOLE_ARCHIVE
)

# Headers are target sources so a CMake file-API client (CLion) attaches them
# to this target; CMake adds no compile step for a header.
target_sources(${COMPONENT_LIB} PRIVATE
    "assert_test_support.hpp"
    "assert_unity_support.hpp"
    "static_string_assert_cases.hpp"
    "static_vector_assert_cases.hpp"
)

# Standalone assert-trigger programs.
#
# The Unity tests fork+exec these programs to verify that contract violations
# abort, without aborting the test runner itself. Each is built as a plain host
# executable (no IDF runtime) into the build directory next to the test ELF, so
# the corresponding test can locate it via a path derived from /proc/self/exe.
# The `_assert_cases.hpp` headers are shared between the trigger program and
# the Unity test; the component's own include directory comes from
# `libutility`.
function(add_assert_program name)
    add_executable(${name} "test_${name}.cpp")
    target_link_libraries(${name} PRIVATE idf::libutility)
    target_include_directories(${name} PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}"
    )
    set_target_properties(${name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    )
    # These are plain executables, not component libraries, so the project's
    # flag-helper loops do not reach them. The sanitizer calls also put both
    # runtimes on their link line: they link instrumented components.
    thermo_enable_gcc_analyzer(${name})
    thermo_enable_compiler_warnings(${name})
    thermo_enable_ubsan(${name})
    thermo_enable_asan(${name})
endfunction()

add_assert_program(static_string_assert)
add_assert_program(static_vector_assert)
