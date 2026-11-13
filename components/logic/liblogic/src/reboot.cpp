
// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_system.h"
#pragma GCC diagnostic pop
// NOLINTEND

#include "logic/reboot.hpp"

namespace thermo {

[[noreturn]]
void reboot() {
    // Give logging time to flush out over serial
    //
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();
}

}  // namespace thermo
