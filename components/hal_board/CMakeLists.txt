# hal_board: hardware abstraction implementations for the development board.
#
# Provides the real, GPIO-backed hardware drivers (currently the 3-position
# mode switch). The production firmware (`main/`) depends on this component;
# the QEMU/test projects depend on `hal_stub` instead, which exposes the same
# `hal/switch.hpp`. A project must depend on exactly one of the two.
#
# Both depend on the header-only `hal_common` component for the shared switch
# vocabulary (`PinState`, `Pins`, `SwitchLike`).

idf_component_register(
    SRCS
        "switch.cpp"
    INCLUDE_DIRS
        "include"
    REQUIRES
        hal_common
        esp_driver_gpio
)
