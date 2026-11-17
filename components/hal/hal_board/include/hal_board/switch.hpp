// Hardware abstraction: switch input, real GPIO implementation for the
// development board.
//
// The shared switch vocabulary (`SwitchState`, `SwitchLike`) lives in
// the header-only `hal_common` component (`hal/switch_types.hpp`); this
// header adds the concrete, GPIO-backed `Switch` class. `hal_stub`
// provides an identical `Switch` (same ctor, inline `create()` factory,
// `init()`, `read()`), so a project states its choice by including
// `<hal_board/switch.hpp>`.
//
// The switch is reported without semantics: `read()` returns `kOn` while the
// switch is closed and `kOff` while it is open. A registered interrupt
// callback (`set_interrupt()`) is invoked on every state change. Interpreting
// further (debouncing, hold timing) is the caller's job.
#pragma once

// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#include "esp_err.h"
#pragma GCC diagnostic pop
// NOLINTEND

#include "hal/switch_types.hpp"

namespace thermo::hal::board {

// Reads a switch wired (with a pull-up) to a GPIO.
class Switch {
  public:
    // `gpio` is the input pin.
    explicit Switch(int gpio) noexcept;

    Switch(Switch const&) = delete;
    // Movable so `create()` (and containers/factories) can hand a Switch
    // back by value without needing guaranteed copy elision.
    Switch(Switch&&) noexcept = default;
    ~Switch() = default;
    Switch& operator=(Switch const&) = delete;
    Switch& operator=(Switch&&) = delete;

    // Builds a switch for the given input pin. Same signature for the hardware
    // and stub implementations; call init() before read().
    [[nodiscard]] static Switch create(int gpio) noexcept {
        return Switch(gpio);
    }

    // Configures the input pin. Must be called before read().
    esp_err_t init() noexcept;

    // Samples the pin and returns the switch's current position:
    // `SwitchState::kOn` when closed, `SwitchState::kOff` when open.
    [[nodiscard]] SwitchState read() const noexcept;

    // Registers the callback invoked from the GPIO interrupt whenever the
    // switch's state changes, passing the new state. Call this *after* init()
    // (the handler is installed on the pin, which init() configures).
    //
    // The GPIO ISR service must already be installed (gpio_install_isr_service)
    // before registering a handler; this HAL does not install it.
    //
    // Passing `std::nullopt` (or `{}`) unregisters any callback, so nothing is
    // invoked on a state change; polling via read() still works.
    void set_interrupt(SwitchHandler handler) noexcept;

  private:
    int const gpio_;
};

static_assert(SwitchLike<Switch>, "Switch must satisfy the SwitchLike concept");

}  // namespace thermo::hal::board
