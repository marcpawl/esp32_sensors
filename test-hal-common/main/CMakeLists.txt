# Test component for the implementation-free `hal_common` vocabulary.
#
# Builds for the ESP-IDF `linux` target and runs the Unity test runner:
#
#   test_hal_board_types.cpp    `BoardLike` (including every rejection) and the
#                               `BoardPins` defaults from `hal/pin_map.hpp`.
#   test_hal_sleep_types.cpp    The deep-sleep vocabulary: `WakeCause`,
#                               `WakeCauses`, `wake_cause_bit()`, `woke_from()`.
#   test_hal_timer_types.cpp    `TimerLike` and `OneShotTimerFactoryLike`, with
#                               model types that pin each rejection.
#
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
