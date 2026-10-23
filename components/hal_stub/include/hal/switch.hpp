// Hardware abstraction: 3-position ON-OFF-ON mode switch (§2.3), deterministic
// stub for the host tests.
//
// The shared switch vocabulary (`PinState`, `Pins`, `SwitchLike`) lives in the
// header-only `hal_common` component (`hal/switch_types.hpp`); this header adds
// the software-driven `Switch` class. `hal_board` provides an identical
// `Switch` (same ctor, inline `create()` factory, `init()`, `read()`) so a
// project can link either one and include `<hal/switch.hpp>` unchanged.
//
// The switch is reported without semantics: the HAL only returns the raw state
// of the two sense pins. Mapping pin states to Configure / Off / Run is the
// caller's job.
//
// This stub replaces the GPIO-backed `hal_board` implementation where the sense
// pins do not exist. The reported pin states are held in software and can be
// changed with `set_pins()`, so bad hardware (both pins on, or both pins off)
// can be simulated.
#pragma once

#include "esp_err.h"
#include "hal/switch_types.hpp"

namespace thermo::hal {

// Reads a 3-position ON-OFF-ON switch wired to two sense pins.
class Switch {
  public:
    // `first_gpio` / `third_gpio` are the two sense pins (the outer poles).
    // The stub ignores them.
    Switch(int first_gpio, int third_gpio) noexcept;

    Switch(Switch const&) = delete;
    // Movable so `create()` (and containers/factories) can hand a Switch back
    // by value without needing guaranteed copy elision.
    Switch(Switch&&) noexcept = default;
    ~Switch() = default;
    Switch& operator=(Switch const&) = delete;
    Switch& operator=(Switch&&) = delete;

    // Builds a switch for the two given sense pins. Same signature for the
    // hardware and stub implementations; call init() before read().
    [[nodiscard]] static Switch create(int first_gpio,
                                       int third_gpio) noexcept {
        return Switch(first_gpio, third_gpio);
    }

    // No-op for the stub (there are no pins to configure); always succeeds.
    esp_err_t init() noexcept;

    // Returns the pin states set by `set_pins()`, defaulting to the Third
    // position (first off, third on).
    [[nodiscard]] Pins read() const noexcept;

    // Sets the pin states reported by subsequent `read()` calls. Intended for
    // host tests, including simulating bad hardware.
    void set_pins(Pins pins) noexcept;

  private:
    Pins pins_{PinState::kOff, PinState::kOn};
};

static_assert(SwitchLike<Switch>, "Switch must satisfy the SwitchLike concept");

}  // namespace thermo::hal
