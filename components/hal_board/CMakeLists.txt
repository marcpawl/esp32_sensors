# hal_board: hardware abstraction implementations for the development board.
#
# Provides the real, GPIO-backed hardware drivers (the 3-position mode switch,
# the LEDs, and the DS18B20 1-Wire bus) plus the driver-backed internal
# chip-temperature sensor. The production firmware (`main/`) depends on this
# component; the QEMU/test projects depend on `hal_stub` instead, which exposes
# the same `hal/switch.hpp`, `hal/led.hpp`, `hal/onewire.hpp`, and
# `hal/chip_temperature.hpp`. A project must depend on exactly one of the two.
#
# Both depend on the header-only `hal_common` component for the shared device
# vocabulary (`PinState`, `Pins`, `SwitchLike`, `LedState`, `LedLike`,
# `OneWireLike`, `ChipTemperatureLike`).

idf_component_register(
    SRCS
        "switch.cpp"
        "led.cpp"
        "push_button.cpp"
        "onewire.cpp"
        "chip_temperature.cpp"
    INCLUDE_DIRS
        "include"
    REQUIRES
        hal_common
        esp_driver_gpio
        esp_driver_tsens
)
