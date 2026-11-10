# Test component for the `espidf_cpp` wrappers.
#
# Builds for the ESP-IDF `linux` target and runs the Unity test runner:
#
#   test_one_message_queue.cpp   OneMessageQueue<T>: creation, overwrite
#                                semantics, send/receive round-trips, timeouts,
#                                move semantics, and the raw handle.
#   test_static_vector.cpp       static_vector<T, N>: storage, growth, and the
#                                aborting and try_* overflow paths.
#   test_static_string.cpp       static_string<N>: always-NUL-terminated,
#                                bounded append, try_push_back/try_append.
#   test_http_server_types.cpp   The HttpServer vocabulary: HttpMethod,
#                                HttpStatus, parse_int, normalize_url, fields.
#   test_http_server.cpp         HttpServer<BodyCapacity> over a real loopback
#                                socket: prefix routing, registration,
#                                GET/POST fields, and every HTTP-error path.
#
# `espidf_cpp` (the component under test) is a normal requirement;
# `hal_stub` is a private requirement for parity with the firmware's test
# project (and for future tests of HAL-facing wrappers). WHOLE_ARCHIVE is
# required so the TEST_CASE registrations in the test sources are linked into
# the final executable even though nothing references them.

set(test_srcs
    "test_main.cpp"
    "test_one_message_queue.cpp"
    "test_static_vector.cpp"
    "test_static_string.cpp"
    "test_http_server_types.cpp"
    "test_http_server.cpp"
)

idf_component_register(
    SRCS ${test_srcs}
    INCLUDE_DIRS
        "."
    REQUIRES
        espidf_cpp
    PRIV_REQUIRES
        unity
        hal_stub
    WHOLE_ARCHIVE
)

# Standalone assert-trigger programs.
#
# The Unity tests fork+exec these programs to verify that contract violations
# abort, without aborting the test runner itself. Each is built as a plain host
# executable (no IDF runtime) into the build directory next to the test ELF, so
# the corresponding test can locate it via a path derived from /proc/self/exe.
# The `_assert_cases.hpp` headers are shared between the trigger program and
# the Unity test; the component's own include directory comes from `espidf_cpp`.
function(add_assert_program name)
    add_executable(${name} "test_${name}.cpp")
    target_link_libraries(${name} PRIVATE idf::espidf_cpp)
    target_include_directories(${name} PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}"
    )
    set_target_properties(${name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    )
endfunction()

add_assert_program(static_string_assert)
add_assert_program(static_vector_assert)
