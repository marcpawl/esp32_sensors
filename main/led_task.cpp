// LED status task: drives the LED indication from a message queue.

#include "esp_log.h"
#include "freertos/FreeRTOS.h"

#include "constants.hpp"
#include "driver/gpio.h"
#include "led_task.hpp"

namespace thermo {
namespace {

// Not `kTag`: `Task<LedTask>` has a private member of that name, which would be
// found first by unqualified lookup inside LedTask's members.
constexpr const char* kLedTag = "led_task";

// Configures both LED pins as push-pull outputs and turns them off. Runs on the
// LED task before any `change_leds` call.
void init_led_pins() {
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = (1ULL << pins::kRedLed) | (1ULL << pins::kGreenLed);
    cfg.mode = GPIO_MODE_OUTPUT;
    cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.intr_type = GPIO_INTR_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&cfg));
    ESP_ERROR_CHECK(gpio_set_level(static_cast<gpio_num_t>(pins::kRedLed), 0));
    ESP_ERROR_CHECK(
        gpio_set_level(static_cast<gpio_num_t>(pins::kGreenLed), 0));
}

}  // namespace

[[noreturn]] void LedTask::run() noexcept {
    init_led_pins();
    LedState state = LedState::kBoot;
    while (true) {
        state = on_state_changed(state);
    }
}

LedState LedTask::on_state_changed(LedState new_state) noexcept {
    const Cycles& table = cycles();
    const std::size_t index = static_cast<std::size_t>(new_state);
    assert(index < table.size());
    return std::visit(
        [this](const auto& cycle) -> LedState { return execute_cycle(cycle); },
        table[index]);
}

void LedTask::change_leds(Step const& step) noexcept {
    gpio_set_level(static_cast<gpio_num_t>(pins::kRedLed),
                   step.red_led_ == RedLed::On ? 1 : 0);
    gpio_set_level(static_cast<gpio_num_t>(pins::kGreenLed),
                   step.green_led_ == GreenLed::On ? 1 : 0);
}

template<std::size_t N>
[[nodiscard]] LedState LedTask::execute_cycle(const Cycle<N>& cycle) noexcept {
    while (true) {
        for (Step const& step : cycle.steps_) {
            change_leds(step);

            // Wait for a state change for this step's duration; if one
            // arrives, stop the cycle and hand the new state back.
            if (auto next = receive(step.duration_)) {
                return *next;
            }
        }
    }
    // Unreachable: the loop above never exits normally.
    [[assume(false)]];
    return LedState::kOff;
}

}  // namespace thermo
