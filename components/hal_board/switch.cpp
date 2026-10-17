// Real 3-position ON-OFF-ON mode switch implementation for the dev board
// (§2.3). Reads the two sense GPIOs into a `Pins` value; no debouncing or
// interpretation happens here.
#include "hal/switch.hpp"

#include "driver/gpio.h"

namespace thermo::hal {
namespace {

gpio_config_t input_config(int gpio) {
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = 1ULL << gpio;
    cfg.mode = GPIO_MODE_INPUT;
    cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    // The switch is expected to drive the pin to a defined level; pull-downs
    // keep the reading deterministic if a pole is momentarily open.
    cfg.pull_down_en = GPIO_PULLDOWN_ENABLE;
    cfg.intr_type = GPIO_INTR_DISABLE;
    return cfg;
}

PinState to_pin_state(int level) {
    return level ? PinState::kOn : PinState::kOff;
}

}  // namespace

Switch::Switch(int first_gpio, int third_gpio) noexcept
    : first_gpio_(first_gpio), third_gpio_(third_gpio) {}

esp_err_t Switch::init() noexcept {
    auto first = input_config(first_gpio_);
    auto third = input_config(third_gpio_);
    esp_err_t err = gpio_config(&first);
    if (err != ESP_OK) {
        return err;
    }
    return gpio_config(&third);
}

Pins Switch::read() const noexcept {
    Pins pins;
    pins.first =
        to_pin_state(gpio_get_level(static_cast<gpio_num_t>(first_gpio_)));
    pins.third =
        to_pin_state(gpio_get_level(static_cast<gpio_num_t>(third_gpio_)));
    return pins;
}

}  // namespace thermo::hal
