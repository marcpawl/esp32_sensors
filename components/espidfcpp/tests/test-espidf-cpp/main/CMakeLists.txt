# `libespidfcpp` (the component under test) is a normal requirement;
# `hal_stub` is a private requirement for parity with the firmware's test
# project (and for future tests of HAL-facing wrappers). WHOLE_ARCHIVE is
# required so the TEST_CASE registrations in the test sources are linked into
# the final executable even though nothing references them.

set(test_srcs
    "test_main.cpp"
    "test_mutex.cpp"
    "test_one_message_queue.cpp"
    "test_http_server_types.cpp"
    "test_http_server.cpp"
    "test_timer.cpp"
    # Host implementation of the esp_timer C API, which ESP-IDF does not
    # provide for the `linux` target.
    "fake_esp_timer.cpp"
)

idf_component_register(
    SRCS ${test_srcs}
    INCLUDE_DIRS
        "."
    REQUIRES
        libespidfcpp
    PRIV_REQUIRES
        unity
        hal_stub
    WHOLE_ARCHIVE
)

# GCC's analyzer emits a locationless uninitialized-value diagnostic from the
# std::string concatenations in test_http_server.cpp. A diagnostic pragma
# cannot scope a locationless diagnostic, so disable that one category for
# this file only; it stays fatal in the rest of the suite.
set_source_files_properties(
    "test_http_server.cpp"
    PROPERTIES COMPILE_OPTIONS "-Wno-analyzer-use-of-uninitialized-value")

# GCC's analyzer cannot model how std::optional initialises its payload from
# Lock's move constructor, and reports a use of an uninitialized value while
# inlining Lock::try_lock() in test_mutex.cpp. The wrapper's move is correct
# (it exchanges the mutex pointer and the moved-from Lock is empty), so
# disable that one category for this file only; it stays fatal everywhere else.
set_source_files_properties(
    "test_mutex.cpp"
    PROPERTIES COMPILE_OPTIONS "-Wno-analyzer-use-of-uninitialized-value")
