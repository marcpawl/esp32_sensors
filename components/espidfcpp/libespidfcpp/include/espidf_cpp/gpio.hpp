// Shared strong type for a GPIO pin number.
//
// Making the pin its own type keeps
// a pin from being passed where some other number belongs (a temperature, an
// index, a mode) and back: the constructor is `explicit` and there is no
// implicit conversion out, so an `int` only becomes a `Gpio` when the caller
// says so.

#pragma once

// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#include "driver/gpio.h"
#pragma GCC diagnostic pop

// NOLINTEND

namespace thermo::espidf_cpp {

// A GPIO pin number. Cheap to copy, constexpr throughout, and explicit in both
// directions: `Gpio{4}` builds one, `as_gpio_num_t()` reads it back.
class Gpio {
  public:
    // GPIO 0. `BoardPins` uses the default for a pin the reference board does
    // not wire (the plain user switch).
    constexpr Gpio() noexcept = default;

    // The pin number, as ESP-IDF's `gpio_num_t` would name it.
    explicit constexpr Gpio(gpio_num_t gpio) noexcept : gpio_(gpio) {}

    Gpio(Gpio const&) noexcept = default;
    Gpio(Gpio&&) noexcept = default;
    ~Gpio() noexcept = default;
    Gpio& operator=(Gpio const&) noexcept = default;
    Gpio& operator=(Gpio&&) noexcept = default;

    // The pin number, for the driver boundary and for diagnostics. This is the
    // only way out of the type, so every conversion is greppable.
    [[nodiscard]] constexpr gpio_num_t as_gpio_num_t() const noexcept {
        return gpio_;
    }

    friend constexpr bool operator==(Gpio const&, Gpio const&) = default;

  private:
    gpio_num_t gpio_ = GPIO_NUM_0;
};

}  // namespace thermo::espidf_cpp
