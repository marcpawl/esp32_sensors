# Test component for the pin-free `hal_stub` devices.
#
# Builds for the ESP-IDF `linux` target and runs the Unity test runner:
#
#   test_hal_led.cpp              The stub LED: `init()`, `write()`, `state()`.
#   test_hal_push_button.cpp      The stub push button: `init()`, `read()`,
#                                 `set()`, `set_interrupt()`.
#   test_hal_onewire.cpp          The stub 1-Wire bus: `discover_sensors()`,
#                                 `set_sensors()`, `read()`.
#   test_hal_chip_temperature.cpp The stub chip-temperature sensor.
#   test_hal_wifi.cpp             The stub WiFi radio: AP/station start/stop.
#   test_hal_timer_stub.cpp       The stub one-shot timer and its factory.
#   test_freertos_timers.cpp      The FreeRTOS software timers the stub timer
#                                 is built on (`freertos/timers.h`).
#   test_freertos_task.cpp        The FreeRTOS task primitive the firmware
#                                 relies on (`vTaskDelay()`).
#   test_hal_board.cpp            The aggregate `BoardStub`: device wiring and
#                                 the board-wide deep sleep.
#
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
