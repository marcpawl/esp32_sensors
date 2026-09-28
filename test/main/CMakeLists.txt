# Unit-test component.
#
# Compiles the ESP-IDF-free production sources directly from ../main so the
# tests exercise real firmware code rather than copies. Only modules that do
# not depend on chip-specific IDF drivers are listed here; hardware-facing code
# (1-Wire GPIO, Wi-Fi, HTTPS) is covered by on-target tests instead.
#
# WHOLE_ARCHIVE is required so the TEST_CASE registrations in the test sources
# are linked into the final executable even though nothing references them.

set(test_srcs
    "test_main.cpp"
    "test_temp_format.cpp"
    "test_payload.cpp"
    "test_config_validation.cpp"
    "test_rom_id.cpp"
    "test_rtc_buffer.cpp"
)

# Production sources under test (paths are relative to this component dir).
set(sut_srcs
    "${CMAKE_CURRENT_LIST_DIR}/../../main/temp_format.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../main/payload.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../main/app_config.cpp"
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
    WHOLE_ARCHIVE
)
