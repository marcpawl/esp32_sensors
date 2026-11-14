# A project must depend on exactly one of `hal_board` and `hal_stub`: the two
# expose the same API, so a project links one implementation.
#
# The timer is the one device whose header is *not* the same name in both
# implementations (`hal_board/timer_board.hpp` here, `hal_stub/timer_stub.hpp`
# in `hal_stub`): the two share only the `hal_common` concepts, not a `Timer`
# class, so board-independent code is written against
# `OneShotTimerFactoryLike` instead of including either header.
#
# `esp_hw_support` is a public requirement because deep sleep (§3.8) is part of
# the board interface: `board.cpp` calls `esp_sleep_enable_timer_wakeup()` /
# `esp_deep_sleep_start()` and `esp_sleep_get_wakeup_causes()`, which live
# there. `esp_timer` is a public requirement because
# `hal_board/timer_board.hpp` names `esp_timer_handle_t` in the handle class.

idf_component_register(
    SRCS
        "led.cpp"
        "push_button.cpp"
        "onewire.cpp"
        "chip_temperature.cpp"
        "wifi.cpp"
        "timer_board.cpp"
        "board.cpp"
    INCLUDE_DIRS
        "include"
    REQUIRES
        hal_common
        esp_driver_gpio
        esp_driver_tsens
        # Deep sleep (§3.8): `esp_sleep_*` (also the owner of esp_sleep.h).
        esp_hw_support
        # One-shot timer: `esp_timer_*` and `esp_timer.h`.
        esp_timer
    # WiFi (SoftAP + station): driver, netif glue, and event loop.
    PRIV_REQUIRES
        esp_wifi
        esp_netif
        esp_event
)
