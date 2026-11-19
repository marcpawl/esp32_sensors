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

#include <utility>

namespace thermo::hal::stub {

Switch::Switch(int gpio) noexcept : gpio_(gpio) {}

Switch::~Switch() noexcept {
    remove_interrupt();
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
    // Simulate the hardware interrupt: notify the registered callback
    // (no-op when none is registered). The callback reads the new state.
    if (handler_) {
        handler_();
    }
}

void Switch::set_interrupt(SwitchCallback handler) noexcept {
    handler_ = std::move(handler);
}

void Switch::remove_interrupt() noexcept {
    handler_ = {};
}

}  // namespace thermo::hal::stub
