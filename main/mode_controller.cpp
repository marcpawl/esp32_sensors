// 3-position ON-OFF-ON switch reading (§2.3, §3).
#include "mode_controller.hpp"

#include "driver/gpio.h"
#include "esp_log.h"

namespace thermo {
namespace {

constexpr const char* kTag = "mode";

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

}  // namespace

ModeController::ModeController(int config_gpio, int run_gpio)
    : config_gpio_(config_gpio), run_gpio_(run_gpio) {}

esp_err_t ModeController::init() {
    auto cfg = input_config(config_gpio_);
    auto run = input_config(run_gpio_);
    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        return err;
    }
    return gpio_config(&run);
}

Mode ModeController::read() const {
    const int cfg_level = gpio_get_level(static_cast<gpio_num_t>(config_gpio_));
    const int run_level = gpio_get_level(static_cast<gpio_num_t>(run_gpio_));

    if (cfg_level && run_level) {
        // Both HIGH should not occur with a well-formed switch; treat as
        // Configure, which is the safer (non-transmitting) interpretation.
        ESP_LOGW(kTag, "both mode pins HIGH; defaulting to Configure");
        return Mode::kConfigure;
    }
    if (cfg_level) {
        return Mode::kConfigure;
    }
    if (run_level) {
        return Mode::kRun;
    }
    return Mode::kOff;
}

}  // namespace thermo
