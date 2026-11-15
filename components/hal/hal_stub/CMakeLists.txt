# A project must depend on exactly one of `hal_stub` and `hal_board`: the two
# expose the same API, so a project links one implementation. The stub's
# aggregate board is `BoardStub` (`hal_stub/board_stub.hpp`) where `hal_board`
# offers `Board` (`hal_board/board.hpp`), so a project can tell which
# implementation it linked; code that must work with either takes the
# `hal::BoardLike` template parameter.
#
# Deep sleep (§3.8) needs no ESP-IDF dependency here: the stub implements it
# entirely in software.

idf_component_register(
    SRCS
        "led_stub.cpp"
        "push_button_stub.cpp"
        "onewire_stub.cpp"
        "chip_temperature_stub.cpp"
        "wifi_stub.cpp"
        "esp_timer_stub.cpp"
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
