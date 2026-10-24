// Real single-LED implementation for the dev board (§2.1, §7). Configures the
// GPIO as an output and drives it to the requested level.
// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#include "driver/gpio.h"
#pragma GCC diagnostic pop
// NOLINTEND

#include "hal/led.hpp"

namespace thermo::hal {

Led::Led(int gpio, const char* name) noexcept : gpio_(gpio) {
    // Accepted for interface parity with the stub implementation; unused.
    (void)name;
}

esp_err_t Led::init() noexcept {
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = 1ULL << gpio_;
    cfg.mode = GPIO_MODE_OUTPUT;
    cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.intr_type = GPIO_INTR_DISABLE;
    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        return err;
    }
    return gpio_set_level(static_cast<gpio_num_t>(gpio_), 0);
}

void Led::write(LedState state) noexcept {
    gpio_set_level(static_cast<gpio_num_t>(gpio_),
                   state == LedState::kOn ? 1 : 0);
}

}  // namespace thermo::hal
