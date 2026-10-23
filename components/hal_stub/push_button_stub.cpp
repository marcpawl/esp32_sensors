// Deterministic push-button stub for the host tests.
//
// There is no input pin to read, so the reported state is held in software.
// `init()` always succeeds and `read()` returns whatever `set()` last set
// (default: released). A callback registered with `set_interrupt()` is invoked
// synchronously by `set()` whenever the state actually changes.
#include "hal/push_button.hpp"

namespace thermo::hal {

PushButton::PushButton(int gpio) noexcept {
    // Accepted for interface parity with the real implementation; unused.
    (void)gpio;
}

esp_err_t PushButton::init() noexcept {
    return ESP_OK;
}

ButtonState PushButton::read() const noexcept {
    return state_;
}

void PushButton::set(ButtonState state) noexcept {
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

void PushButton::set_interrupt(ButtonHandler handler) noexcept {
    handler_ = handler;
}

}  // namespace thermo::hal
