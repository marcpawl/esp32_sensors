// The switch is reported without semantics: `read()` returns `kOn` while the
// switch is closed and `kOff` while it is open. `gpio()` reports the pin the
// switch was built for, so callers can name it in diagnostics. A registered
// interrupt callback (`set_interrupt()`) is invoked on every state change and
// reads the new state through `read()`. Interpreting further (debouncing, hold
// timing) is the caller's job.
#pragma once

#include <functional>

// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#include "esp_err.h"
#pragma GCC diagnostic pop
// NOLINTEND

#include "espidf_cpp/gpio.hpp"
#include "espidf_cpp/interrupt_handle.hpp"
#include "utility/isr_safe.hpp"

namespace thermo::espidf_cpp {

// State of a switch. `kOff` is open, `kOn` is closed.
enum class SwitchState : std::uint8_t {
    kOff = 0,
    kOn,
};

// Handler invoked when the switch's state changes. It takes no argument: the
// switch's new state is read through the switch itself (`Switch::read()`).
// This lets a handler capture the object it belongs to.
//
// The handler runs in the GPIO interrupt, so it must be ISR-safe: no blocking,
// no heap, no mutex. `set_interrupt()` annotates its parameter
// `[[thermo::isr_safe]]` to make that requirement explicit.
//
// The `noexcept` in the function type is part of the ISR contract, not
// decoration: a callback that might throw cannot be stored, so a lambda
// without `noexcept` refuses to convert. The wrapper is move-only because a
// registration has exactly one owner and is only ever moved into the switch.
//
// An empty callback means "no handler"; passing one to `set_interrupt()`
// unregisters the callback, so nothing is invoked on a state change.
using SwitchCallback = std::move_only_function<void() noexcept>;

// Reads a switch wired (with a pull-up) to a GPIO.
class Switch {
  public:
    // `gpio` is the input pin.
    explicit Switch(Gpio gpio) noexcept;

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

    [[nodiscard]] static Switch create(Gpio gpio) noexcept
    {
        return Switch(gpio);
    }

    // Configures the input pin. Must be called before read().
    esp_err_t init() noexcept;

    // Samples the pin and returns the switch's current position:
    // `SwitchState::kOn` when closed, `SwitchState::kOff` when open.
    [[nodiscard]] SwitchState read() const noexcept;

    // The input pin this switch was built for, as passed to `create()`.
    [[nodiscard]] Gpio gpio() const noexcept { return gpio_; }

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
    Gpio const gpio_;
    // Owns the ISR registration, so the destructor cannot forget to remove it.
    // Moving transfers it and leaves the source idle.
    InterruptHandle interrupt_handle_;
};

}  // namespace thermo::espidf_cpp
