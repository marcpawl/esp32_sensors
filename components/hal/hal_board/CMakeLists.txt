# A project must depend on exactly one of `hal_board` and `hal_stub`: the two
# expose the same API, so a project links one implementation.
#
# `esp_hw_support` is a public requirement because deep sleep (§3.8) is part of
# the board interface: `board.cpp` calls `esp_sleep_enable_timer_wakeup()` /
# `esp_deep_sleep_start()` and `esp_sleep_get_wakeup_causes()`, which live
# there.

idf_component_register(
    SRCS
        "led.cpp"
        "push_button.cpp"
        "onewire.cpp"
        "chip_temperature.cpp"
        "wifi.cpp"
        "board.cpp"
    INCLUDE_DIRS
        "include"
    REQUIRES
        hal_common
        esp_driver_gpio
        esp_driver_tsens
        # Deep sleep (§3.8): `esp_sleep_*` (also the owner of esp_sleep.h).
        esp_hw_support
    # WiFi (SoftAP + station): driver, netif glue, and event loop.
    PRIV_REQUIRES
        esp_wifi
        esp_netif
        esp_event
)
