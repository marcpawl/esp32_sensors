# The ESP-IDF-free production sources are compiled directly from the `mcgowan`
# subproject (`../../mcgowan/src`) rather than linked, so the tests exercise
# real firmware code and not copies; modules that need chip-specific drivers
# are covered by on-target tests instead.
#
# Header-only modules need no `sut_srcs` entry — the test sources include them
# directly. The `liblogic` component compiles its own sources, so its `.cpp` files
# are never listed here; the tests pull them in via PRIV_REQUIRES.
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
    "test_led_runner.cpp"
    "test_reboot.cpp"
    "test_sequence_lock.cpp"
)

# Production sources under test (paths are relative to this component dir).
set(sut_srcs
    "${CMAKE_CURRENT_LIST_DIR}/../../../../../mcgowan/src/app_config.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../../../../mcgowan/src/config_page.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../../../../mcgowan/src/power_source.cpp"
)

idf_component_register(
    SRCS ${test_srcs} ${sut_srcs}
    INCLUDE_DIRS
        "."
        "${CMAKE_CURRENT_LIST_DIR}/../../../../../mcgowan/src/include"
    PRIV_REQUIRES
        unity
        libespidfcpp
        liblogic
        hal_stub
    WHOLE_ARCHIVE
)
