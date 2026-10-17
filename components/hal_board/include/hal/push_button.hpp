// Hardware abstraction: push button input, real GPIO implementation for the
// development board.
//
// The shared push-button vocabulary (`ButtonState`, `PushButtonLike`) lives in
// the header-only `hal_common` component (`hal/push_button_types.hpp`); this
// header adds the concrete, GPIO-backed `PushButton` class. `hal_stub`
// provides an identical `PushButton` (same ctor, inline `create()` factory,
// `init()`, `read()`) so a project can link either one and include
// `<hal/push_button.hpp>` unchanged.
//
// The button is reported without semantics: `read()` returns `kOn` while the
// button is pressed and `kOff` while it is released. A registered interrupt
// callback (`set_interrupt()`) is invoked on every state change. Interpreting
// further (debouncing, hold timing) is the caller's job.
#pragma once

#include "esp_err.h"
#include "hal/push_button_types.hpp"

namespace thermo::hal {

// Reads a push button wired (with a pull-up) to a GPIO.
class PushButton {
  public:
    // `gpio` is the input pin.
    explicit PushButton(int gpio) noexcept;

    PushButton(PushButton const&) = delete;
    // Movable so `create()` (and containers/factories) can hand a PushButton
    // back by value without needing guaranteed copy elision.
    PushButton(PushButton&&) noexcept = default;
    ~PushButton() = default;
    PushButton& operator=(PushButton const&) = delete;
    PushButton& operator=(PushButton&&) = delete;

    // Builds a button for the given input pin. Same signature for the hardware
    // and stub implementations; call init() before read().
    [[nodiscard]] static PushButton create(int gpio) noexcept {
        return PushButton(gpio);
    }

    // Configures the input pin. Must be called before read().
    esp_err_t init() noexcept;

    // Samples the pin and returns whether the button is currently pressed:
    // `ButtonState::kOn` when pressed, `ButtonState::kOff` when released.
    [[nodiscard]] ButtonState read() const noexcept;

    // Registers the callback invoked from the GPIO interrupt whenever the
    // button's state changes, passing the new state. Call this *after* init()
    // (the handler is installed on the pin, which init() configures).
    //
    // The GPIO ISR service must already be installed (gpio_install_isr_service)
    // before registering a handler; this HAL does not install it.
    //
    // Passing `std::nullopt` (or `{}`) unregisters any callback, so nothing is
    // invoked on a state change; polling via read() still works.
    void set_interrupt(ButtonHandler handler) noexcept;

  private:
    int const gpio_;
};

static_assert(PushButtonLike<PushButton>,
              "PushButton must satisfy the PushButtonLike concept");

}  // namespace thermo::hal
