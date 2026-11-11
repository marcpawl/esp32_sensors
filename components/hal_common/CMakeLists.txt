# `hal/sleep_types.hpp` and `hal/timer_types.hpp` stay free of ESP-IDF
# dependencies on purpose: `esp_sleep.h` is not self-contained on the `linux`
# target (it includes `hal/gpio_types.h`), the ESP-IDF linux port links no
# `sleep_modes.c`, and it likewise has no `esp_timer` implementation.
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
