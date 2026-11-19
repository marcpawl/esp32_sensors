# A project must depend on exactly one of `hal_stub` and `hal_board`: the two
# expose the same API, so a project links one implementation. The stub's
# aggregate board is `BoardStub` (`hal_stub/board_stub.hpp`) where `hal_board`
# offers `Board` (`hal_board/board.hpp`), so a project can tell which
# implementation it linked; code that must work with either takes the
# `hal::BoardLike` template parameter.
#
# Deep sleep (§3.8) needs no ESP-IDF dependency here: the stub implements it
# entirely in software.
#
# `gpio_stub.cpp` supplies `gpio_install_isr_service()` for the `linux` target,
# where `esp_driver_gpio` registers an empty component (no sources, no headers).

idf_component_register(
    SRCS
        "led_stub.cpp"
        "switch_stub.cpp"
        "onewire_stub.cpp"
        "chip_temperature_stub.cpp"
        "wifi_stub.cpp"
        "esp_timer_stub.cpp"
        "gpio_stub.cpp"
        "board_stub.cpp"
    INCLUDE_DIRS
        "include"
    REQUIRES
        hal_common
    # `esp_timer_stub.cpp` implements the `esp_timer` C API for the host; the
    # IDF component supplies only the headers on `linux`, and `esp_etm.h` (via
    # `esp_timer.h`) comes from `esp_hw_support`. FreeRTOS provides the
    # software timers the stub is built on.
    PRIV_REQUIRES
        esp_timer
        esp_hw_support
        freertos
)

# Headers are target sources so a CMake file-API client (CLion) attaches them
# to this target; CMake adds no compile step for a header.
target_sources(${COMPONENT_LIB} PRIVATE
    "include/hal_stub/board_stub.hpp"
    "include/hal_stub/chip_temperature.hpp"
    "include/hal_stub/led.hpp"
    "include/hal_stub/onewire.hpp"
    "include/hal_stub/switch.hpp"
    "include/hal_stub/wifi.hpp"
)
