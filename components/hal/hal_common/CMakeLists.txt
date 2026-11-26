# `hal/sleep_types.hpp` stays free of ESP-IDF dependencies on purpose:
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
        # `hal/onewire_types.hpp` names `utility::static_vector` in the shared
        # `OneWireLike` concept, so the container is a public requirement.
        libutility
        esp_common
)

# Headers are target sources so a CMake file-API client (CLion) attaches them
# to this target; CMake adds no compile step for a header.
target_sources(${COMPONENT_LIB} PRIVATE
    "include/hal/board_types.hpp"
    "include/hal/chip_temperature_types.hpp"
    "include/hal/ds18b20_types.hpp"
    "include/hal/gpio.hpp"
    "include/hal/led_types.hpp"
    "include/hal/onewire_types.hpp"
    "include/hal/pin_map.hpp"
    "include/hal/sleep_types.hpp"
    "include/hal/wifi_types.hpp"
)
