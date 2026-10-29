# hal_board: hardware abstraction implementations for the development board.
#
# Provides the real, GPIO-backed hardware drivers (the LEDs, the push buttons,
# and the DS18B20 1-Wire bus), the driver-backed internal chip-temperature
# sensor, and the driver-backed WiFi radio (the SoftAP and station,
# `hal/wifi.hpp`), aggregated into `hal/board.hpp`. The production firmware
# (`main/`) depends on this component; the host test projects depend on
# `hal_stub` instead, which exposes the same
# `hal/led.hpp`, `hal/push_button.hpp`, `hal/onewire.hpp`,
# `hal/chip_temperature.hpp`, `hal/wifi.hpp`, and `hal/board.hpp`. A project
# must depend on exactly one of the two.
#
# Both depend on the header-only `hal_common` component for the shared device
# vocabulary (`ButtonState`, `PushButtonLike`, `LedState`, `LedLike`,
# `OneWireLike`, `ChipTemperatureLike`, `AccessPointConfig`, `StationConfig`,
# `WifiRadioLike`, `BoardPins`, `BoardLike`) and the deep-sleep vocabulary
# (`WakeCause`, `WakeCauses`, `wake_cause_bit()`, `woke_from()`).
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
