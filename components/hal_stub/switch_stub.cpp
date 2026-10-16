// Deterministic stub of the 3-position mode switch for a simulated
// environment.
//
// There are no sense pins to read, so the reported pin states are held in
// software. `init()` always succeeds and `read()` returns whatever
// `set_pins()` last set (default: the Third position).
#include "hal/switch.hpp"

namespace thermo::hal {

Switch::Switch(int first_gpio, int third_gpio) noexcept {
    // Accepted for interface parity with the real implementation; unused.
    (void)first_gpio;
    (void)third_gpio;
}

esp_err_t Switch::init() noexcept {
    return ESP_OK;
}

Pins Switch::read() const noexcept {
    return pins_;
}

void Switch::set_pins(Pins pins) noexcept {
    pins_ = pins;
}

}  // namespace thermo::hal
