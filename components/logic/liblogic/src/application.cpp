
#include <chrono>

#include "logic/application.hpp"

// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#include "esp_log.h"
#pragma GCC diagnostic pop
#include "../../../../../usr/include/c++/13/chrono"
#include "espidf_cpp/task.hpp"

namespace thermo::logic {

Application::Application(Board& board) : board_(board) {}

void Application::application_main(void) {
    ESP_LOGI(kTag, "Starting application");

    // Delay to allow switches to stabilize.
    // Instead of debouncing, we will stabilize, and just reboot
    // when there is a change between run and config.
    constexpr std::chrono::milliseconds stabilization_delay{200};
    espidf_cpp::vTaskDelay(stabilization_delay);

    while (true) {
        espidf_cpp::vTaskDelay(std::chrono::seconds{1});
    }
}

}  // namespace thermo::logic