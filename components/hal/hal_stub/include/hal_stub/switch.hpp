// Hardware abstraction: switch input, deterministic stub for the
// host tests.
//
// The shared switch vocabulary (`SwitchState`, `SwitchLike`) lives in
// the header-only `hal_common` component (`hal/switch_types.hpp`); this
// header adds the software-driven `Switch` class. `hal_board` provides an
// identical `Switch` (same ctor, inline `create()` factory, `init()`,
// `read()`, `gpio()`, `set_interrupt()`), so a project states its choice by
// including `<hal_stub/switch.hpp>`.
//
// This stub replaces the GPIO-backed `hal_board` implementation where the
// input pin does not exist. The reported state is held in software and can be
// changed with `set()`, so a close, an open, and any on/off sequence can
// be simulated. A callback registered with `set_interrupt()` is invoked
// synchronously by `set()` whenever the state actually changes.
#pragma once

// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#include "esp_err.h"
#pragma GCC diagnostic pop
// NOLINTEND

#include "hal/switch_types.hpp"

namespace thermo::hal::stub {

// Represents a switch whose state is held in software.
class Switch {
  public:
    // `gpio` is kept for interface parity and reported by `gpio()`; the stub
    // does not touch a pin.
    explicit Switch(Gpio gpio) noexcept;

    Switch(Switch const&) = delete;
    // Movable so `create()` (and containers/factories) can hand a Switch
    // back by value without needing guaranteed copy elision.
    Switch(Switch&&) noexcept = default;
    // Releases any handler, matching the hardware switch's destructor.
    ~Switch() noexcept;
    Switch& operator=(Switch const&) = delete;
    Switch& operator=(Switch&&) = delete;

    // Builds a stub switch for the given pin. Same signature for the hardware
    // and stub implementations; call init() before read().
    [[nodiscard]] static Switch create(Gpio gpio) noexcept {
        // GCC's analyzer cannot model libstdc++'s std::function internals and
        // reports an uninitialized read while returning the switch by value.
        // The code is correct; keep the diagnostic fatal everywhere else.
        // NOLINTBEGIN
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wanalyzer-use-of-uninitialized-value"
        return Switch(gpio);
#pragma GCC diagnostic pop
        // NOLINTEND
    }

    // No-op for the stub (there is no pin to configure); always succeeds.
    esp_err_t init() noexcept;

    // Returns the state set by `set()`, defaulting to open (`kOff`).
    [[nodiscard]] SwitchState read() const noexcept;

    // The pin this switch was built for, as passed to `create()`.
    [[nodiscard]] Gpio gpio() const noexcept { return gpio_; }

    // Sets the state reported by subsequent `read()` calls. Intended for host
    // tests. If the state actually changes and a callback is
    // registered (see `set_interrupt()`), the callback is invoked synchronously
    // with the new state (simulating the hardware interrupt).
    void set(SwitchState state) noexcept;

    // Registers the callback invoked whenever `set()` changes the state. The
    // callback takes no argument: it reads the new state through `read()`. The
    // stub has no real interrupt, so the callback runs on the caller's thread,
    // synchronously inside `set()`.
    //
    // Passing an empty `SwitchCallback` unregisters the callback, so nothing is
    // invoked on a state change; polling via read() still works.
    void set_interrupt([[thermo::isr_safe]] SwitchCallback handler) noexcept;

    // Unregisters the callback installed by set_interrupt(), if any. After
    // this call `set()` invokes nothing until a new callback is registered.
    // Idempotent, and a no-op on a moved-from switch.
    void remove_interrupt() noexcept;

  private:
    Gpio gpio_{};
    SwitchState state_ = SwitchState::kOff;
    SwitchCallback handler_;
};

static_assert(SwitchLike<Switch>, "Switch must satisfy the SwitchLike concept");

}  // namespace thermo::hal::stub
