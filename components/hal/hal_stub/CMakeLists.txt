# A project must depend on exactly one of `hal_stub` and `hal_board`: the two
# expose the same API, so a project links one implementation. The stub's
# aggregate board is `BoardStub` (`hal_stub/board_stub.hpp`) where `hal_board`
# offers `Board` (`hal_board/board.hpp`), so a project can tell which
# implementation it linked; code that must work with either takes the
# `hal::BoardLike` template parameter.
#
# The one-shot timer is the one device whose header is *not* the same name in
# both implementations (`hal_stub/timer_stub.hpp` here,
# `hal_board/timer_board.hpp` in `hal_board`): the two share only the
# `hal_common` concepts, not a `Timer` class, so board-independent code is
# written against `OneShotTimerFactoryLike` instead of including either header.
# `timer_stub.cpp` is built on FreeRTOS
# software timers (`freertos/timers.h`), because the `linux` target has no
# `esp_timer` implementation.
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
        "timer_stub.cpp"
        "board_stub.cpp"
    INCLUDE_DIRS
        "include"
    REQUIRES
        hal_common
)
