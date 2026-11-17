// Deterministic switch stub for the host tests.
//
// There is no input pin to read, so the reported state is held in software.
// `init()` always succeeds and `read()` returns whatever `set()` last set
// (default: open). A callback registered with `set_interrupt()` is invoked
// synchronously by `set()` whenever the state actually changes.
// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#pragma GCC diagnostic pop
// NOLINTEND

#include "hal_stub/switch.hpp"

namespace thermo::hal::stub {

Switch::Switch(int gpio) noexcept {
    // Accepted for interface parity with the real implementation; unused.
    (void)gpio;
}

esp_err_t Switch::init() noexcept {
    return ESP_OK;
}

SwitchState Switch::read() const noexcept {
    return state_;
}

void Switch::set(SwitchState state) noexcept {
    if (state == state_) {
        return;
    }
    state_ = state;
    // Simulate the hardware interrupt: notify the registered callback of the
    // new state (no-op when none is registered).
    if (handler_.has_value()) {
        handler_.value()(state_);
    }
}

void Switch::set_interrupt(SwitchHandler handler) noexcept {
    handler_ = handler;
}

}  // namespace thermo::hal::stub
