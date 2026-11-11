# `hal_stub` is the component under test. WHOLE_ARCHIVE is required so the
# TEST_CASE registrations in the test sources are linked into the final
# executable even though nothing references them.

set(test_srcs
    "test_main.cpp"
    "test_hal_led.cpp"
    "test_hal_push_button.cpp"
    "test_hal_onewire.cpp"
    "test_hal_chip_temperature.cpp"
    "test_hal_wifi.cpp"
    "test_hal_timer_stub.cpp"
    "test_freertos_timers.cpp"
    "test_freertos_task.cpp"
    "test_esp_restart.cpp"
    "test_hal_board.cpp"
)

idf_component_register(
    SRCS ${test_srcs}
    INCLUDE_DIRS
        "."
    PRIV_REQUIRES
        unity
        hal_stub
    WHOLE_ARCHIVE
)
