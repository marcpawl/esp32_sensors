# The shared component holds every target-neutral case; these sources are the
# pieces that cannot be linked by the chip project. `libespidfcpp` is needed
# directly by the socket and timer cases, `hal_stub` by the fake timer's
# target, and `unity` by the runner. WHOLE_ARCHIVE registers these cases too.

set(test_srcs
    "test_main.cpp"
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
        test-espidf-cpp-lib
        libespidfcpp
    PRIV_REQUIRES
        unity
        hal_stub
    WHOLE_ARCHIVE
)

# Headers are target sources so a CMake file-API client (CLion) attaches them
# to this target; CMake adds no compile step for a header.
target_sources(${COMPONENT_LIB} PRIVATE "fake_esp_timer.hpp")

# GCC's static analyzer emits a locationless uninitialized-value diagnostic
# from the std::string concatenations in test_http_server.cpp. A diagnostic
# pragma cannot scope a locationless diagnostic, so disable that one category
# for this file only; it stays fatal in the rest of the suite.
set_source_files_properties(
    "test_http_server.cpp"
    PROPERTIES COMPILE_OPTIONS "-Wno-analyzer-use-of-uninitialized-value")
