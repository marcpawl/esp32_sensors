// Hardware abstraction: push button input, deterministic stub for the
// host tests.
//
// The shared push-button vocabulary (`ButtonState`, `PushButtonLike`) lives in
// the header-only `hal_common` component (`hal/push_button_types.hpp`); this
// header adds the software-driven `PushButton` class. `hal_board` provides an
// identical `PushButton` (same ctor, inline `create()` factory, `init()`,
// `read()`) so a project can link either one and include
// `<hal/push_button.hpp>` unchanged.
//
// This stub replaces the GPIO-backed `hal_board` implementation where the
// input pin does not exist. The reported state is held in software and can be
// changed with `set()`, so a press, a release, and any press/hold sequence can
// be simulated. A callback registered with `set_interrupt()` is invoked
// synchronously by `set()` whenever the state actually changes.
#pragma once

// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#include "esp_err.h"
#pragma GCC diagnostic pop
// NOLINTEND

#include "hal/push_button_types.hpp"

namespace thermo::hal {

// Represents a push button whose state is held in software.
class PushButton {
  public:
    // `gpio` is ignored by the stub.
    explicit PushButton(int gpio) noexcept;

    PushButton(PushButton const&) = delete;
    // Movable so `create()` (and containers/factories) can hand a PushButton
    // back by value without needing guaranteed copy elision.
    PushButton(PushButton&&) noexcept = default;
    ~PushButton() = default;
    PushButton& operator=(PushButton const&) = delete;
    PushButton& operator=(PushButton&&) = delete;

    // Builds a stub button for the given pin. Same signature for the hardware
    // and stub implementations; call init() before read().
    [[nodiscard]] static PushButton create(int gpio) noexcept {
        return PushButton(gpio);
    }

    // No-op for the stub (there is no pin to configure); always succeeds.
    esp_err_t init() noexcept;

    // Returns the state set by `set()`, defaulting to released (`kOff`).
    [[nodiscard]] ButtonState read() const noexcept;

    // Sets the state reported by subsequent `read()` calls. Intended for host
    // tests. If the state actually changes and a callback is
    // registered (see `set_interrupt()`), the callback is invoked synchronously
    // with the new state (simulating the hardware interrupt).
    void set(ButtonState state) noexcept;

    // Registers the callback invoked whenever `set()` changes the state,
    // passing the new state. The stub has no real interrupt, so the callback
    // runs on the caller's thread, synchronously inside `set()`.
    //
    // Passing `std::nullopt` (or `{}`) unregisters any callback, so nothing is
    // invoked on a state change; polling via read() still works.
    void set_interrupt(ButtonHandler handler) noexcept;

  private:
    ButtonState state_ = ButtonState::kOff;
    ButtonHandler handler_ = std::nullopt;
};

static_assert(PushButtonLike<PushButton>,
              "PushButton must satisfy the PushButtonLike concept");

}  // namespace thermo::hal
