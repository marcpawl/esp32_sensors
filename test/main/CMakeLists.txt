# Unit-test component.
#
# Compiles the ESP-IDF-free production sources directly from the `mcgowan`
# subproject (../../mcgowan/src) so the tests exercise real firmware code rather
# than copies. Only modules that do
# not depend on chip-specific IDF drivers are listed here; hardware-facing code
# (1-Wire GPIO, Wi-Fi, HTTPS) is covered by on-target tests instead.
#
# Header-only modules need no `sut_srcs` entry — they are included directly by
# the test sources. `logic/mode.hpp` / `logic/mode_selection.hpp` are the
# example: the mode-sense pole mapping is pure and `constexpr`, so
# `test_mode_selection.cpp` exercises the real headers with no extra source.
#
# The pin-free HAL stubs (hal_stub's led, push button, onewire bus,
# chip temperature, wifi radio, and the aggregate board) are host-testable too
# and are pulled in via PRIV_REQUIRES below; test_hal_led.cpp,
# test_hal_push_button.cpp, test_hal_onewire.cpp, test_hal_chip_temperature.cpp,
# test_hal_wifi.cpp, and test_hal_board.cpp exercise them.
# test_led_runner.cpp instantiates the templated LedRunner<Board> against the
# stub board.
#
# The LED task itself lives in the shared `logic` component
# (`components/logic/include/logic/led_task.hpp`), not in `mcgowan/src/`, so
# the tests include it as `logic/led_task.hpp` and pull the component in via
# PRIV_REQUIRES below. The same goes for the RTC state and its buffer
# operations (`logic/rtc_state.hpp`, `logic/src/rtc_buffer.cpp`): they are
# compiled by the `logic` component, so `test_rtc_buffer.cpp` includes
# `logic/rtc_state.hpp` and lists no RTC source in `sut_srcs`. Every .cpp file
# in `logic` is compiled by that component, not listed here.
#
# WHOLE_ARCHIVE is required so the TEST_CASE registrations in the test sources
# are linked into the final executable even though nothing references them.

set(test_srcs
    "test_main.cpp"
    "test_temp_format.cpp"
    "test_payload.cpp"
    "test_config_validation.cpp"
    "test_config_page.cpp"
    "test_rom_id.cpp"
    "test_rtc_buffer.cpp"
    "test_mode_selection.cpp"
    "test_hal_led.cpp"
    "test_hal_push_button.cpp"
    "test_hal_onewire.cpp"
    "test_hal_chip_temperature.cpp"
    "test_hal_wifi.cpp"
    "test_hal_board.cpp"
    "test_led_runner.cpp"
)

# Production sources under test (paths are relative to this component dir).
set(sut_srcs
    "${CMAKE_CURRENT_LIST_DIR}/../../mcgowan/src/payload.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../mcgowan/src/app_config.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../mcgowan/src/config_page.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../mcgowan/src/power_source.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../mcgowan/src/rom_id.cpp"
)

idf_component_register(
    SRCS ${test_srcs} ${sut_srcs}
    INCLUDE_DIRS
        "."
        "${CMAKE_CURRENT_LIST_DIR}/../../mcgowan/src/include"
    PRIV_REQUIRES
        unity
        espidf_cpp
        logic
        hal_stub
    WHOLE_ARCHIVE
)
