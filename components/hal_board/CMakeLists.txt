# hal_board: hardware abstraction implementations for the development board.
#
# Provides the real, GPIO-backed hardware drivers (currently the 3-position
# mode switch). The production firmware (`main/`) depends on this component;
# the QEMU/test projects depend on `hal_stub` instead, which exposes the same
# headers. A project must depend on exactly one of the two.

idf_component_register(
    SRCS
        "switch.cpp"
    INCLUDE_DIRS
        "include"
    REQUIRES
        esp_driver_gpio
)
