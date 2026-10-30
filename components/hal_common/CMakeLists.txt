# hal_common: shared, implementation-free HAL vocabulary.
#
# Header-only component defining the types common to every HAL device
# implementation (the push button's `ButtonState` and `PushButtonLike`; the
# LED's `LedState` and `LedLike`; the 1-Wire bus's `OneWireLike`; the WiFi
# radio's `AccessPointConfig`, `StationConfig`, and `WifiRadioLike`; the
# chip temperature sensor's `ChipTemperatureLike`; and the board-level deep
# sleep vocabulary `WakeCause` / `WakeCauses` with `wake_cause_bit()` and
# `woke_from()`). It carries no hardware dependency, so both `hal_board` and
# `hal_stub` can require it and the shared definitions live in exactly one
# place.
#
# `hal/sleep_types.hpp` stays dependency-free (only `<cstdint>`) on purpose:
# `esp_sleep.h` is not self-contained on the `linux` target (it includes
# `hal/gpio_types.h`), and the ESP-IDF linux port links no `sleep_modes.c`.
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
