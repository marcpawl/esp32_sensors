// LED status task: drives the LED indication from a message queue.

#include "esp_log.h"
#include "freertos/FreeRTOS.h"

#include "led_task.hpp"

namespace thermo {
namespace {

// Not `kTag`: `Task<LedTask>` has a private member of that name, which would be
// found first by unqualified lookup inside LedTask's members.
constexpr const char* kLedTag = "led_task";

}  // namespace

[[noreturn]] void LedTask::run() noexcept {
    for (;;) {
        if (auto state = receive(portMAX_DELAY)) {
            on_state_changed(*state);
        }
    }
}

// TODO: stub — replace the log line with the real LED transitions
// (LedManager).
void LedTask::on_state_changed(LedState new_state) noexcept {
    ESP_LOGI(kLedTag, "LED state -> %d (stub; not yet implemented)",
             static_cast<int>(new_state));
}

}  // namespace thermo
