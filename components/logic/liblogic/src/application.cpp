
#include <chrono>

#include "logic/application.hpp"

// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#include "esp_log.h"
#pragma GCC diagnostic pop

#include "logic/reboot.hpp"
#include "espidf_cpp/task.hpp"

namespace thermo::logic {

Application::Application(Board& board) : board_(board) {}

void Application::update_mode() {
    // Read the mode switches.
    espidf_cpp::SwitchState switch_config_state = board_.config_switch().read();
    espidf_cpp::SwitchState switch_run_state = board_.run_switch().read();
    mode_ = mode_from_switches(switch_config_state, switch_run_state);
}

void Application::initialize_state() {
    // Minimum abount of time to delay to allow switches to debounce.
    std::chrono::milliseconds debounce_delay {500};

    auto before = std::chrono::steady_clock::now();
    auto after = std::chrono::steady_clock::now();
    auto duration = after - before;
    if (duration < debounce_delay) {
        std::chrono::milliseconds more_delay = std::chrono::duration_cast<std::chrono::milliseconds>(debounce_delay - duration);
        espidf_cpp::vTaskDelay(more_delay);
    }

    update_mode();
}

[[noreturn]] void Application::configure_state() {
    ESP_LOGI(kTag, "Configure_state");
    do {
        espidf_cpp::vTaskDelay(std::chrono::seconds{5});
    } while (false);
    logic::reboot();
}


[[noreturn]] void Application::run_state() {
    ESP_LOGI(kTag, "run_state");
    do {
        espidf_cpp::vTaskDelay(std::chrono::seconds{5});
    } while (false);
    logic::reboot();
}

[[noreturn]] void Application::off_state() {
    ESP_LOGI(kTag, "off_state");
    logic::reboot();
}

[[noreturn]]
void Application::application_main(void) {
    ESP_LOGI(kTag, "Starting application");

    initialize_state();
    if (! mode_.has_value()) {
        ESP_LOGE(kTag, "Mode switch did not return a valid state");
        logic::reboot();
     std::unreachable();
    }

    auto mode = mode_.value();
    if (mode == Mode::kConfigure) {
        configure_state();
        std::unreachable();
    } else if (mode == Mode::kRun) {
        run_state();
        std::unreachable();
    } else if (mode == Mode::kOff) {
        off_state();
        std::unreachable();
    } else {
        ESP_LOGE(kTag, "Mode switch did not return a valid state");
        espidf_cpp::vTaskDelay(std::chrono::seconds{60});
        logic::reboot();
        std::unreachable();
    }
}

}  // namespace thermo::logic