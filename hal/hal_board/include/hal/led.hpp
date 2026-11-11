// Hardware abstraction: single LED output (§2.1, §7), real GPIO implementation
// for the development board.
//
// The shared LED vocabulary (`LedState`, `LedLike`) lives in the header-only
// `hal_common` component (`hal/led_types.hpp`); this header adds the concrete,
// GPIO-backed `Led` class. `hal_stub` provides an identical `Led` (same ctor,
// inline `create()` factory, `init()`, `write()`) so a project can link either
// one and include `<hal/led.hpp>` unchanged.
#pragma once

// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#include "esp_err.h"
#pragma GCC diagnostic pop
// NOLINTEND

#include "hal/led_types.hpp"

namespace thermo::hal {

// Drives one LED wired (through a current-limiting resistor) to a GPIO.
class Led {
  public:
    // `gpio` is the output pin. `name` is a human-readable label; the hardware
    // implementation does not use it (it exists for parity with the stub,
    // which logs it).
    Led(int gpio, const char* name) noexcept;

    Led(Led const&) = delete;
    // Movable so `create()` (and containers/factories) can hand an Led back by
    // value without needing guaranteed copy elision.
    Led(Led&&) noexcept = default;
    ~Led() = default;
    Led& operator=(Led const&) = delete;
    Led& operator=(Led&&) = delete;

    // Builds an LED for the given output pin. Same signature for the hardware
    // and stub implementations; call init() before write().
    [[nodiscard]] static Led create(int gpio, const char* name) noexcept {
        return Led(gpio, name);
    }

    // Configures the pin as an output and drives it off. Must be called before
    // write().
    esp_err_t init() noexcept;

    // Drives the LED to the requested state.
    void write(LedState state) noexcept;

  private:
    int const gpio_;
};

static_assert(LedLike<Led>, "Led must satisfy the LedLike concept");

}  // namespace thermo::hal
