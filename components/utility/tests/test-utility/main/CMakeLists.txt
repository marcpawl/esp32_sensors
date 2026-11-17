# `libutility` (the component under test) is a normal requirement. WHOLE_ARCHIVE
# is required so the TEST_CASE registrations in the test sources are linked into
# the final executable even though nothing references them.

set(test_srcs
    "test_main.cpp"
    "test_static_vector.cpp"
    "test_static_string.cpp"
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
    # `thermo_enable_gcc_analyzer()` and `thermo_enable_compiler_warnings()`
    # loops do not reach them.
    thermo_enable_gcc_analyzer(${name})
    thermo_enable_compiler_warnings(${name})
endfunction()

add_assert_program(static_string_assert)
add_assert_program(static_vector_assert)
