// Real NTP implementation for the dev board (§5.3, §5.4).
//
// Backed by the ESP-IDF SNTP client (`esp_sntp`): reconfigure SNTP for the
// requested server, poll the system clock until it is set, and convert to Unix
// epoch seconds. Retries the wait up to `retry_count` times with `timeout_ms`
// each; returns `nullopt` if every attempt fails.
#include "hal/network_time_protocol.hpp"

#include <cassert>
#include <ctime>

#include "esp_log.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace thermo::hal {
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

NetworkTimeProtocol::NetworkTimeProtocol() noexcept = default;

NetworkTimeProtocol::~NetworkTimeProtocol() noexcept {
    // Stop the SNTP client if a query left it running.
    if (esp_sntp_enabled()) {
        esp_sntp_stop();
    }
}

esp_err_t NetworkTimeProtocol::init() noexcept {
    // Nothing to install up front: the SNTP client is configured on demand by
    // get_utc_epoch() (it depends on the server, which is a per-query
    // argument).
    initialized_ = true;
    return ESP_OK;
}

std::optional<std::uint32_t> NetworkTimeProtocol::get_utc_epoch(
    const std::string& server, int retry_count,
    std::uint32_t timeout_ms) noexcept {
    assert(initialized_ &&
           "NetworkTimeProtocol::init() must be called before get_utc_epoch()");

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

    for (int attempt = 1; attempt <= retry_count; ++attempt) {
        ESP_LOGI(kTag, "NTP sync attempt %d/%d via %s", attempt, retry_count,
                 server.c_str());
        if (wait_for_sync(timeout_ms)) {
            const std::time_t now = std::time(nullptr);
            esp_sntp_stop();
            return static_cast<std::uint32_t>(now);
        }
    }

    ESP_LOGW(kTag, "NTP sync failed after %d attempts", retry_count);
    esp_sntp_stop();
    return std::nullopt;
}

}  // namespace thermo::hal
