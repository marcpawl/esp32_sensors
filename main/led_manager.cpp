// LED status indication (§7).
#include "led_manager.hpp"

#include "constants.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

namespace thermo {
namespace {

constexpr const char* kTag = "led";

gpio_config_t output_config(int gpio) {
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = 1ULL << gpio;
    cfg.mode = GPIO_MODE_OUTPUT;
    cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.intr_type = GPIO_INTR_DISABLE;
    return cfg;
}

std::uint32_t now_ms() {
    return static_cast<std::uint32_t>(esp_timer_get_time() / 1000);
}

} // namespace

esp_err_t LedManager::init() {
    // Both LEDs become outputs and start OFF.
    auto red = output_config(pins::kRedLed);
    auto green = output_config(pins::kGreenLed);
    ESP_ERROR_CHECK(gpio_config(&red));
    ESP_ERROR_CHECK(gpio_config(&green));
    all_off();
    return ESP_OK;
}

void LedManager::set_green(bool on) {
    green_on_ = on;
    gpio_set_level(static_cast<gpio_num_t>(pins::kGreenLed), on ? 1 : 0);
}

void LedManager::set_red(bool on) {
    red_on_ = on;
    gpio_set_level(static_cast<gpio_num_t>(pins::kRedLed), on ? 1 : 0);
}

void LedManager::all_off() {
    inhibited_ = false;
    fatal_ = false;
    set_red(false);
    set_green(false);
}

void LedManager::set_configure_mode() {
    inhibited_ = false;
    fatal_ = false;
    set_red(true);
    set_green(true);
}

void LedManager::boot_flash() {
    set_green(true);
    set_red(true);
    vTaskDelay(pdMS_TO_TICKS(timing::kBootFlashMs));
    set_green(false);
    set_red(false);
}

void LedManager::set_sampling() {
    inhibited_ = false;
    set_red(false);
    set_green(false);
}

void LedManager::set_uploading() {
    inhibited_ = false;
    set_red(false);
    set_green(true);
}

void LedManager::upload_ok_pulse() {
    set_green(true);
    set_red(false);
    vTaskDelay(pdMS_TO_TICKS(timing::kUploadOkPulseMs));
    set_green(false);
}

void LedManager::set_tx_error() {
    inhibited_ = false;
    fatal_ = false;
    set_red(true);
    set_green(false);
}

void LedManager::recovered_pulse() {
    set_red(false);
    set_green(true);
    vTaskDelay(pdMS_TO_TICKS(timing::kRecoveredSolidMs));
    set_green(false);
}

void LedManager::set_fatal() {
    inhibited_ = false;
    fatal_ = true;
    set_green(false);
    last_blink_ms_ = now_ms();
    blink_phase_ = true;
    set_red(true);
}

void LedManager::set_inhibited() {
    inhibited_ = true;
    fatal_ = false;
    set_green(false);
    // Enter the blink cycle at the start of an ON phase.
    last_blink_ms_ = now_ms();
    blink_phase_ = true;
    set_red(true);
}

void LedManager::poll(std::uint32_t now_ms_value) {
    if (fatal_) {
        // §7 fatal error: blink at 0.5 Hz (1 s period) with 50% duty.
        if (now_ms_value - last_blink_ms_ >= 1000) {
            last_blink_ms_ = now_ms_value;
            blink_phase_ = !blink_phase_;
            set_red(blink_phase_);
        }
        return;
    }
    if (inhibited_) {
        // §7 inhibited: 500 ms blink every 10 s.
        const std::uint32_t elapsed = now_ms_value - last_blink_ms_;
        if (blink_phase_) {
            if (elapsed >= timing::kInhibitBlinkMs) {
                set_red(false);
                blink_phase_ = false;
                last_blink_ms_ = now_ms_value;
            }
        } else if (elapsed >=
                   static_cast<std::uint32_t>(timing::kInhibitBlinkPeriodMs -
                                              timing::kInhibitBlinkMs)) {
            set_red(true);
            blink_phase_ = true;
            last_blink_ms_ = now_ms_value;
        }
    }
}

} // namespace thermo
