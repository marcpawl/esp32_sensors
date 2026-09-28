// NTP time acquisition for the save handler (§5.3, §5.4).
#include "ntp_client.hpp"

#include <ctime>

#include "constants.hpp"
#include "esp_log.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace thermo {
namespace {

constexpr const char* kTag = "ntp";

// Waits for the system clock to be set, up to `timeout_ms`.
bool wait_for_sync(std::uint32_t timeout_ms) {
    const std::uint32_t start = xTaskGetTickCount() * portTICK_PERIOD_MS;
    while (true) {
        const std::time_t now = std::time(nullptr);
        // Anything past 2020-01-01 (~1.577e9) implies a real NTP time.
        if (now > 1577836800) {
            return true;
        }
        const std::uint32_t elapsed =
            xTaskGetTickCount() * portTICK_PERIOD_MS - start;
        if (elapsed >= timeout_ms) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

}  // namespace

std::optional<std::uint32_t> ntp_get_utc_epoch(const std::string& server) {
    // (Re)configure SNTP for the requested server. The Wi-Fi interface must
    // already be up; in Configure mode this relies on the connected station
    // being available, which for a first-boot AP-only device may fail — in
    // that case we return nullopt and the caller reports a warning (§5.3).
    if (esp_sntp_enabled()) {
        esp_sntp_stop();
    }
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, server.c_str());
    esp_sntp_init();

    for (int attempt = 1; attempt <= timing::kNtpRetryCount; ++attempt) {
        ESP_LOGI(kTag, "NTP sync attempt %d/%d via %s", attempt,
                 timing::kNtpRetryCount, server.c_str());
        if (wait_for_sync(timing::kNtpTimeoutMs)) {
            const std::time_t now = std::time(nullptr);
            esp_sntp_stop();
            return static_cast<std::uint32_t>(now);
        }
    }

    ESP_LOGW(kTag, "NTP sync failed after %d attempts", timing::kNtpRetryCount);
    esp_sntp_stop();
    return std::nullopt;
}

}  // namespace thermo
