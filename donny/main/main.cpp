// Entry point for the `donny` linux-emulated application.
//
// Builds for the ESP-IDF `linux` target and runs natively on the host. It links
// the production HAL (`hal_board`), `espidf_cpp`, and `logic`; for now it does
// nothing but stay alive, sleeping for one minute per iteration.
// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#pragma GCC diagnostic pop

// NOLINTEND

namespace {

constexpr char kTag[] = "donny";
constexpr TickType_t kSpinPeriod = pdMS_TO_TICKS(60 * 1000);

}  // namespace

extern "C" void app_main(void) {
    ESP_LOGI(kTag, "donny running (linux emulation)");
    while (true) {
        vTaskDelay(kSpinPeriod);
    }
}
