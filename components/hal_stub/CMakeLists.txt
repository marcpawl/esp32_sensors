# hal_stub: stub hardware abstractions for a simulated environment.
#
# Exposes the same `hal/switch.hpp`, `hal/led.hpp`, `hal/push_button.hpp`,
# `hal/onewire.hpp`, `hal/chip_temperature.hpp`,
# `hal/network_time_protocol.hpp`, and `hal/board.hpp` as `hal_board` but with
# deterministic, side-effect-free implementations that need no real hardware.
# Test projects run against this component instead of `hal_board` so they can
# drive the devices from software in a simulated environment; a project must
# depend on exactly one of the two.
#
# Like `hal_board`, it depends on the header-only `hal_common` component for the
# shared device vocabulary (`PinState`, `Pins`, `SwitchLike`, `LedState`,
# `LedLike`, `PushButtonLike`, `OneWireLike`, `ChipTemperatureLike`,
# `NetworkTimeProtocolLike`, `BoardPins`, `BoardLike`).

idf_component_register(
    SRCS
        "switch_stub.cpp"
        "led_stub.cpp"
        "push_button_stub.cpp"
        "onewire_stub.cpp"
        "chip_temperature_stub.cpp"
        "network_time_protocol_stub.cpp"
        "board_stub.cpp"
    INCLUDE_DIRS
        "include"
    REQUIRES
        hal_common
)
