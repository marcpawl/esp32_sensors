# Unity cases shared by the linux and ESP32-S3 libespidfcpp test projects.
# WHOLE_ARCHIVE keeps the TEST_CASE registrations in both final ELFs even
# though no code references them directly.
#
# The fake esp_timer, the socket-driven HTTP server cases, and the runner stay
# in the individual projects: the fake cannot link alongside the real chip
# timer, the socket cases use POSIX loopback, and only the host runner exits.
# The directory name is the ESP-IDF component name; ESP-IDF 6.1 has no
# `NAME` override for `idf_component_register()`.
#
# `test_interrupt_handle.cpp` uses the GPIO ISR API and FreeRTOS. On the chip
# it needs `esp_driver_gpio`; on `linux` `hal_stub` supplies both the mirrored
# declarations and a software model of the ISR registration. `freertos` is
# available on both targets.

if(IDF_TARGET STREQUAL "linux")
    set(GPIO_DEPENDENCY hal_stub)
else()
    set(GPIO_DEPENDENCY esp_driver_gpio)
endif()

idf_component_register(
    SRCS
        "src/test_http_server_types.cpp"
        "src/test_interrupt_handle.cpp"
        "src/test_mutex.cpp"
        "src/test_nvs.cpp"
        "src/test_one_message_queue.cpp"
        "src/test_ring_buffer.cpp"
        "src/test_switch.cpp"
        "src/test_task.cpp"
    PRIV_REQUIRES
        unity
        libespidfcpp
        ${GPIO_DEPENDENCY}
        freertos
    WHOLE_ARCHIVE
)

# GCC's analyzer cannot model how std::optional initialises its payload from
# Lock's move constructor, and reports a use of an uninitialized value while
# inlining Lock::try_lock() in test_mutex.cpp. The wrapper's move is correct
# (it exchanges the mutex pointer and the moved-from Lock is empty), so
# disable that one category for this file only; it stays fatal everywhere else.
set_source_files_properties(
    "src/test_mutex.cpp"
    PROPERTIES COMPILE_OPTIONS "-Wno-analyzer-use-of-uninitialized-value")
