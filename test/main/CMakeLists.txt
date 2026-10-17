# Unit-test component.
#
# Compiles the ESP-IDF-free production sources directly from the `mcgowan`
# subproject (../../mcgowan/src) so the tests exercise real firmware code rather
# than copies. Only modules that do
# not depend on chip-specific IDF drivers are listed here; hardware-facing code
# (1-Wire GPIO, Wi-Fi, HTTPS) is covered by on-target tests instead.
#
# The pin-free HAL stubs (hal_stub's switch, led, push button, onewire bus,
# chip temperature, and the aggregate board) are host-testable too and are
# pulled in via PRIV_REQUIRES below; test_hal_switch.cpp, test_hal_led.cpp,
# test_hal_push_button.cpp, test_hal_onewire.cpp, test_hal_chip_temperature.cpp,
# and test_hal_board.cpp exercise them.
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
    "test_hal_switch.cpp"
    "test_hal_led.cpp"
    "test_hal_push_button.cpp"
    "test_hal_onewire.cpp"
    "test_hal_chip_temperature.cpp"
    "test_hal_board.cpp"
)

# Production sources under test (paths are relative to this component dir).
set(sut_srcs
    "${CMAKE_CURRENT_LIST_DIR}/../../mcgowan/src/temp_format.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../mcgowan/src/payload.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../mcgowan/src/app_config.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../mcgowan/src/config_page.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../mcgowan/src/power_source.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../mcgowan/src/rom_id.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../mcgowan/src/rtc_buffer.cpp"
)

idf_component_register(
    SRCS ${test_srcs} ${sut_srcs}
    INCLUDE_DIRS
        "."
        "${CMAKE_CURRENT_LIST_DIR}/../../mcgowan/src/include"
    PRIV_REQUIRES
        unity
        hal_stub
    WHOLE_ARCHIVE
)
