// 3-position ON-OFF-ON switch reading (§2.3, §3).

#include <chrono>

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "mode_controller.hpp"
#include "reboot.hpp"

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

void ModeController::read_once(ReadCounts& counts) const {
    const int cfg_level = gpio_get_level(static_cast<gpio_num_t>(config_gpio_));
    const int run_level = gpio_get_level(static_cast<gpio_num_t>(run_gpio_));
    counts.attempts_++;
    if (cfg_level) {
        counts.config_++;
    }
    if (run_level) {
        counts.run_++;
    }
}

ModeController::ReadCounts ModeController::read_many_times() const {
    std::chrono::seconds constexpr duration(2);

    const auto start_time = std::chrono::steady_clock::now();
    ReadCounts counts;
    while (std::chrono::steady_clock::now() - start_time < duration) {
        read_once(counts);
        // Yield execution to allow FreeRTOS tasks / watchdog reset (1 RTOS
        // tick)
        vTaskDelay(1);
    }
    return counts;
}

Mode ModeController::read() const {
    ReadCounts const counts = read_many_times();
    int const low_threshold = (counts.attempts_ + 3) / 4;
    int const high_threshold = low_threshold * 3;
    ESP_LOGI(kTag, "attempts %d config %d run %d threshold %d",
             counts.attempts_, counts.config_, counts.run_, high_threshold);
    if (high_threshold < counts.config_) {
        ESP_LOGI(kTag, "configure mode");
        return Mode::kConfigure;
    }
    if (high_threshold < counts.run_) {
        ESP_LOGI(kTag, "run mode");
        return Mode::kRun;
    }

    if ((low_threshold > counts.run_) && (low_threshold > counts.config_)) {
        ESP_LOGI(kTag, "off mode, rebooting");
    } else {
        ESP_LOGE(kTag, "Inconclusive button state, rebooting");
    }

    reboot();
    return Mode::kOff;
}

}  // namespace thermo
