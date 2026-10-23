// Deterministic single-LED stub for the host tests.
//
// There is no output pin to drive, so the last state is held in software and
// each write is logged using the LED's name. `init()` always succeeds.
#include "hal/led.hpp"

#include "esp_log.h"

namespace thermo::hal {
namespace {

constexpr const char* kTag = "hal_led";

const char* state_text(LedState state) noexcept {
    return state == LedState::kOn ? "On" : "Off";
}

}  // namespace

Led::Led(int gpio, const char* name) noexcept : name_(name) {
    // Accepted for interface parity with the real implementation; unused.
    (void)gpio;
}

esp_err_t Led::init() noexcept {
    return ESP_OK;
}

void Led::write(LedState state) noexcept {
    state_ = state;
    ESP_LOGI(kTag, "%s: %s", name_, state_text(state));
}

LedState Led::state() const noexcept {
    return state_;
}

}  // namespace thermo::hal
