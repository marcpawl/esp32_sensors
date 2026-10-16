# hal_common: shared, implementation-free HAL vocabulary.
#
# Header-only component defining the types common to every HAL device
# implementation (the switch's `PinState`, `Pins`, and `SwitchLike`; the LED's
# `LedState` and `LedLike`; the 1-Wire bus's `OneWireLike`; the chip
# temperature sensor's `ChipTemperatureLike`; and the NTP client's
# `NetworkTimeProtocolLike`). It carries no hardware
# dependency, so both `hal_board` and `hal_stub` can require it and the shared
# definitions live in exactly one place.
#
# `esp_common` is required only for `esp_err.h` (the `init()` return type named
# by some of the concepts). No driver is pulled in.
#
# This component is NOT mutually exclusive with anything: it is designed to be
# shared. Only the `hal_board` / `hal_stub` pair is exclusive.

idf_component_register(
    INCLUDE_DIRS
        "include"
    REQUIRES
        esp_common
)
