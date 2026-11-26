// Copy of `hal_common`'s `Gpio` definition, provided by `hal_stub` at the
// same include path so clients use `#include "hal/gpio.hpp"` on both the
// linux and ESP32 targets.
// Shared strong type for a GPIO pin number.
//
// Every HAL device that is wired to a pin names one (`Switch::create(gpio)`,
// `Led::create(gpio, name)`, `OneWire::create(gpio)`, `InterruptHandle`), and
// `BoardPins` describes a wiring with them. Making the pin its own type keeps
// a pin from being passed where some other number belongs (a temperature, an
// index, a mode) and back: the constructor is `explicit` and there is no
// implicit conversion out, so an `int` only becomes a `Gpio` when the caller
// says so.
//
// The stored number is a plain `int`, not ESP-IDF's `gpio_num_t`. `hal_common`
// is implementation-free and builds for the ESP-IDF `linux` target, where the
// GPIO driver is not on the include path (the `linux` port registers
// `esp_driver_gpio` as an empty component), so `gpio_num_t` does not exist
// there. The conversion belongs to the GPIO-backed implementation
// (`hal_board`), which is the only place that calls the driver:
//
//     static_cast<gpio_num_t>(pin.as_int())
//
// so `hal_stub` and every host test can include this header unchanged.
#pragma once

namespace thermo::hal {

// A GPIO pin number. Cheap to copy, constexpr throughout, and explicit in both
// directions: `Gpio{4}` builds one, `as_int()` reads it back.
class Gpio {
  public:
    // GPIO 0. `BoardPins` uses the default for a pin the reference board does
    // not wire (the plain user switch).
    constexpr Gpio() noexcept = default;

    // The pin number, as ESP-IDF's `gpio_num_t` would name it.
    explicit constexpr Gpio(int gpio) noexcept : gpio_(gpio) {}

    // The pin number, for the driver boundary and for diagnostics. This is the
    // only way out of the type, so every conversion is greppable.
    [[nodiscard]] constexpr int as_int() const noexcept { return gpio_; }

    friend constexpr bool operator==(Gpio const&, Gpio const&) = default;

  private:
    int gpio_ = 0;
};

}  // namespace thermo::hal
