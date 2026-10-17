# Test component for the `espidf_cpp` wrappers.
#
# Builds for the ESP-IDF `linux` target and runs the Unity test runner:
#
#   test_one_message_queue.cpp   OneMessageQueue<T>: creation, overwrite
#                                semantics, send/receive round-trips, timeouts,
#                                move semantics, and the raw handle.
#
# `espidf_cpp` (the component under test) is a normal requirement;
# `hal_stub` is a private requirement for parity with the firmware's test
# project (and for future tests of HAL-facing wrappers). WHOLE_ARCHIVE is
# required so the TEST_CASE registrations in the test sources are linked into
# the final executable even though nothing references them.

set(test_srcs
    "test_main.cpp"
    "test_one_message_queue.cpp"
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
