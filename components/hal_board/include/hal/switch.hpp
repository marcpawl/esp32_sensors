// Hardware abstraction: 3-position ON-OFF-ON mode switch (§2.3), real GPIO
// implementation for the development board.
//
// The shared switch vocabulary (`PinState`, `Pins`, `SwitchLike`) lives in the
// header-only `hal_common` component (`hal/switch_types.hpp`); this header adds
// the concrete, GPIO-backed `Switch` class. `hal_stub` provides an identical
// `Switch` (same ctor, inline `create()` factory, `init()`, `read()`) so a
// project can link either one and include `<hal/switch.hpp>` unchanged.
//
// The switch is reported without semantics: the HAL only returns the raw state
// of the two sense pins. Mapping pin states to Configure / Off / Run is the
// caller's job.
#pragma once

#include "esp_err.h"
#include "hal/switch_types.hpp"

namespace thermo::hal {

// Reads a 3-position ON-OFF-ON switch wired to two sense pins.
class Switch {
  public:
    // `first_gpio` / `third_gpio` are the two sense pins (the outer poles).
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

    // Configures the sense pins. Must be called before read().
    esp_err_t init() noexcept;

    // Samples both sense pins and returns their raw state.
    [[nodiscard]] Pins read() const noexcept;

  private:
    int const first_gpio_;
    int const third_gpio_;
};

static_assert(SwitchLike<Switch>, "Switch must satisfy the SwitchLike concept");

}  // namespace thermo::hal
