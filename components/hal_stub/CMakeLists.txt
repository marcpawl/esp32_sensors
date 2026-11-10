# hal_stub: stub hardware abstractions for host tests.
#
# Exposes the HAL API:
# - `hal/led.hpp`
# - `hal/push_button.hpp`
# - `hal/onewire.hpp`,
# - `hal/chip_temperature.hpp`
# - `hal/wifi.hpp` 
# - `hal/board.hpp`. 
# plus the
# aggregate board as `hal/board_stub.hpp` (class `BoardStub`) where `hal_board`
# offers `hal/board.hpp` (class `Board`). The stub's class has its own name so
# a project can tell which implementation it linked; code that must work with
# either takes the `hal::BoardLike` template parameter. The host tests
# (`test-hal-stub/`, `test-hal-common/`, `test/`) depend on this component
# instead of `hal_board`; a project must depend on exactly one of the two.
#
# `hal/wifi.hpp` exposes `WifiRadio` (the `<hal/wifi.hpp>` alias) as the
# software-driven `WifiRadioStub`, where `hal_board` aliases it to the
# driver-backed `WifiRadioBoard`.
#
# The one-shot timer is the one device whose header is *not* the same name in
# both implementations (`hal/timer_stub.hpp` here, `hal/timer_board.hpp` in
# `hal_board`): the two share only the `hal_common` concepts, not a `Timer`
# class, so board-independent code is written against `OneShotTimerFactoryLike`
# instead of including either header. `timer_stub.cpp` is built on FreeRTOS
# software timers (`freertos/timers.h`), because the `linux` target has no
# `esp_timer` implementation.
#
# Like `hal_board`, it depends on the header-only `hal_common` component for the
# shared device vocabulary
# - `ButtonState`
# - `PushButtonLike` 
# - `LedState`
# - `LedLike`,
# - `OneWireLike` 
# - `ChipTemperatureLike` 
# - `AccessPointConfig`
# - `StationConfig`,
# - `WifiRadioLike` 
# - `TimerLike` 
# - `OneShotTimerFactoryLike` 
# - `BoardPins`,
# - `BoardLike` 
# - and the deep-sleep vocabulary: 
#   - `WakeCause` 
#   - `WakeCauses` 
#   - `wake_cause_bit()` 
#   - `woke_from()`
#
# Deep sleep (§3.8) needs no ESP-IDF dependency here: the stub records the
# request, arms the timer wake the real board would produce, and reports it,
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
