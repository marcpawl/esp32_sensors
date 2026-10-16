# Unit-test component.
#
# Compiles the ESP-IDF-free production sources directly from ../main so the
# tests exercise real firmware code rather than copies. Only modules that do
# not depend on chip-specific IDF drivers are listed here; hardware-facing code
# (1-Wire GPIO, Wi-Fi, HTTPS) is covered by on-target tests instead.
#
# The pin-free HAL stubs (hal_stub's switch, led, push button, onewire bus,
# chip temperature, and the aggregate board) are host-testable too and are
# pulled in via PRIV_REQUIRES below; test_hal_switch.cpp, test_hal_led.cpp,
# test_hal_push_button.cpp, test_hal_onewire.cpp, test_hal_chip_temperature.cpp,
# and test_hal_board.cpp exercise them.
#
# The `espidf_cpp` component (Task, OneMessageQueue, and the fixed-capacity
# static_vector/static_string containers) is required below; test_static_vector.cpp
# and test_static_string.cpp exercise the containers.
#
# WHOLE_ARCHIVE is required so the TEST_CASE registrations in the test sources
# are linked into the final executable even though nothing references them.

set(test_srcs
    "test_main.cpp"
    "test_temp_format.cpp"
    "test_payload.cpp"
    "test_config_validation.cpp"
    "test_config_page.cpp"
    "test_config_form.cpp"
    "test_rom_id.cpp"
    "test_rtc_buffer.cpp"
    "test_static_vector.cpp"
    "test_static_string.cpp"
    "test_hal_switch.cpp"
    "test_hal_led.cpp"
    "test_hal_push_button.cpp"
    "test_hal_onewire.cpp"
    "test_hal_chip_temperature.cpp"
    "test_hal_network_time_protocol.cpp"
    "test_hal_board.cpp"
)

# Production sources under test (paths are relative to this component dir).
set(sut_srcs
    "${CMAKE_CURRENT_LIST_DIR}/../../main/temp_format.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../main/payload.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../main/app_config.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../main/config_page.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../main/config_form.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../main/power_source.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../main/rom_id.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../main/rtc_buffer.cpp"
)

idf_component_register(
    SRCS ${test_srcs} ${sut_srcs}
    INCLUDE_DIRS
        "."
        "${CMAKE_CURRENT_LIST_DIR}/../../main/include"
    PRIV_REQUIRES
        unity
        hal_stub
        espidf_cpp
    WHOLE_ARCHIVE
)

# Standalone assert-trigger programs.
#
# The Unity tests fork+exec these programs to verify that contract violations
# abort, without aborting the test runner itself. Each is built as a plain host
# executable (no IDF runtime) into the build directory next to the test ELF, so
# the corresponding test can locate it via a path derived from /proc/self/exe.
function(add_assert_program name)
    add_executable(${name} "test_${name}.cpp")
    target_include_directories(${name} PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}"
        "${CMAKE_CURRENT_LIST_DIR}/../../main/include"
        "${CMAKE_CURRENT_LIST_DIR}/../../components/espidf_cpp/include"
    )
    set_target_properties(${name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    )
endfunction()

add_assert_program(static_string_assert)
add_assert_program(static_vector_assert)
