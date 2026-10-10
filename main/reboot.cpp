
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "reboot.hpp"

namespace thermo {

[[noreturn]]
void reboot()
{
    // Give logging time to flush out over serial
    //
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();
}

}
