# hal_stub: stub hardware abstractions for host tests.
#
# Exposes the same `hal/switch.hpp`, `hal/led.hpp`, `hal/push_button.hpp`,
# `hal/onewire.hpp`, `hal/chip_temperature.hpp`, and `hal/wifi.hpp` as
# `hal_board` (plus `hal/board.hpp`) but with deterministic, side-effect-free
# implementations that need no real hardware. The host tests (`test/`) depend
# on this component instead of `hal_board`; a project must depend on exactly
# one of the two.
#
# `hal/wifi.hpp` exposes `WifiRadio` (the `<hal/wifi.hpp>` alias) as the
# software-driven `WifiRadioStub`, where `hal_board` aliases it to the
# driver-backed `WifiRadioBoard`.
#
# Like `hal_board`, it depends on the header-only `hal_common` component for the
# shared device vocabulary (`PinState`, `Pins`, `SwitchLike`, `LedState`,
# `LedLike`, `PushButtonLike`, `OneWireLike`, `ChipTemperatureLike`,
# `AccessPointConfig`, `StationConfig`, `WifiRadioLike`, `BoardPins`,
# `BoardLike`).

idf_component_register(
    SRCS
        "switch_stub.cpp"
        "led_stub.cpp"
        "push_button_stub.cpp"
        "onewire_stub.cpp"
        "chip_temperature_stub.cpp"
        "wifi_stub.cpp"
        "board_stub.cpp"
    INCLUDE_DIRS
        "include"
    REQUIRES
        hal_common
)
