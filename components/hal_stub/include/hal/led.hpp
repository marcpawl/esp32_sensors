// Hardware abstraction: single LED output (§2.1, §7), deterministic stub for
// the host tests.
//
// The shared LED vocabulary (`LedState`, `LedLike`) lives in the header-only
// `hal_common` component (`hal/led_types.hpp`); this header adds the
// software-driven `Led` class. `hal_board` provides an identical `Led` (same
// ctor, inline `create()` factory, `init()`, `write()`) so a project can link
// either one and include `<hal/led.hpp>` unchanged.
//
// This stub replaces the GPIO-backed `hal_board` implementation where the
// output pin does not exist (or should not be driven). It remembers the last
// state and logs every `write()` using the LED's `name` (e.g. an LED created
// with the name "Green" logs "Green: On"), so the host tests can
// observe LED activity in the log.
#pragma once

#include "esp_err.h"
#include "hal/led_types.hpp"

namespace thermo::hal {

// Represents one LED whose state is held in software.
class Led {
  public:
    // `gpio` is ignored by the stub. `name` is a human-readable label used for
    // logging; it must point to storage that outlives this object (typically a
    // string literal).
    Led(int gpio, const char* name) noexcept;

    Led(Led const&) = delete;
    // Movable so `create()` (and containers/factories) can hand an Led back by
    // value without needing guaranteed copy elision.
    Led(Led&&) noexcept = default;
    ~Led() = default;
    Led& operator=(Led const&) = delete;
    Led& operator=(Led&&) = delete;

    // Builds a stub LED for the given pin and label. Same signature for the
    // hardware and stub implementations; call init() before write().
    [[nodiscard]] static Led create(int gpio, const char* name) noexcept {
        return Led(gpio, name);
    }

    // No-op for the stub (there is no pin to configure); always succeeds.
    esp_err_t init() noexcept;

    // Records the state and logs "<name>: On" or "<name>: Off".
    void write(LedState state) noexcept;

    // Returns the last state passed to write() (default: kOff). Intended for
    // tests, which can also observe writes via the log.
    [[nodiscard]] LedState state() const noexcept;

  private:
    const char* const name_;
    LedState state_ = LedState::kOff;
};

static_assert(LedLike<Led>, "Led must satisfy the LedLike concept");

}  // namespace thermo::hal
