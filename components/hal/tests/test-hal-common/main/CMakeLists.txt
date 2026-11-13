# `hal_common` is the component under test. `hal_stub` is a private
# requirement only because the concepts name concrete device types
# (`ChipTemperature`, `OneWire`, ...), which exist once a HAL implementation is
# linked. WHOLE_ARCHIVE is required so the TEST_CASE registrations in the test
# sources are linked into the final executable even though nothing references
# them.

set(test_srcs
    "test_main.cpp"
    "test_hal_board_types.cpp"
    "test_hal_sleep_types.cpp"
    "test_hal_timer_types.cpp"
)

idf_component_register(
    SRCS ${test_srcs}
    INCLUDE_DIRS
        "."
    REQUIRES
        hal_common
    PRIV_REQUIRES
        unity
        hal_stub
    WHOLE_ARCHIVE
)
