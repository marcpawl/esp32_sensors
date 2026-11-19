// Hardware abstraction: switch input, real GPIO implementation for the
// development board.
//
// The shared switch vocabulary (`SwitchState`, `SwitchLike`) lives in
// the header-only `hal_common` component (`hal/switch_types.hpp`); this
// header adds the concrete, GPIO-backed `Switch` class. `hal_stub`
// provides an identical `Switch` (same ctor, inline `create()` factory,
// `init()`, `read()`, `gpio()`, `set_interrupt()`), so a project states its
// choice by including `<hal_board/switch.hpp>`.
//
// The switch is reported without semantics: `read()` returns `kOn` while the
// switch is closed and `kOff` while it is open. `gpio()` reports the pin the
// switch was built for, so callers can name it in diagnostics. A registered
// interrupt callback (`set_interrupt()`) is invoked on every state change and
// reads the new state through `read()`. Interpreting further (debouncing, hold
// timing) is the caller's job.
#pragma once

// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#include "esp_err.h"
#pragma GCC diagnostic pop
// NOLINTEND

#include "hal/switch_types.hpp"
#include "hal_board/interrupt_handle.hpp"

namespace thermo::hal::board {

// Reads a switch wired (with a pull-up) to a GPIO.
class Switch {
  public:
    // `gpio` is the input pin.
    explicit Switch(int gpio) noexcept;

    Switch(Switch const&) = delete;
    // Movable so `create()` (and containers/factories) can hand a Switch
    // back by value without needing guaranteed copy elision. Moving transfers
    // the interrupt registration, so the moved-from switch owns nothing and
    // its destructor cannot unregister the moved-to switch's handler.
    Switch(Switch&& other) noexcept;
    // Unregisters any handler: an ISR must not call into a destroyed switch.
    ~Switch() noexcept;
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

    // The input pin this switch was built for, as passed to `create()`.
    [[nodiscard]] int gpio() const noexcept { return gpio_; }

    // Registers the callback invoked from the GPIO interrupt whenever the
    // switch's state changes. The callback takes no argument: it reads the
    // new state through `read()`. Call this *after* init() (the handler is
    // installed on the pin, which init() configures).
    //
    // The GPIO ISR service must already be installed (the aggregate Board
    // installs it in its constructor). Each switch gets its own interrupt slot,
    // so several switches can register handlers independently.
    //
    // Passing an empty `SwitchCallback` unregisters the callback, so nothing is
    // invoked on a state change; polling via read() still works.
    void set_interrupt([[thermo::isr_safe]] SwitchCallback handler) noexcept;

    // Unregisters the callback installed by set_interrupt(), if any, and
    // removes the GPIO interrupt. Idempotent, and a no-op on a moved-from
    // switch. After this call nothing is invoked on a state change; polling
    // via read() still works.
    void remove_interrupt() noexcept;

  private:
    int const gpio_;
    // Owns the ISR registration, so the destructor cannot forget to remove it.
    // Moving transfers it and leaves the source idle.
    InterruptHandle interrupt_handle_;
};

static_assert(SwitchLike<Switch>, "Switch must satisfy the SwitchLike concept");

}  // namespace thermo::hal::board
