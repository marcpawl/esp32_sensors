// Deterministic WiFi radio stub for QEMU and host tests.
//
// There is no radio to drive, so the AP and station state is held in software.
// `init()` always succeeds; starting an interface records its configuration
// and marks it up, stopping clears only that interface, and an empty station
// SSID is rejected before any state changes.
#include "hal/wifi.hpp"

#include "esp_log.h"

namespace thermo::hal {
namespace {

constexpr const char* kTag = "hal_wifi";

}  // namespace

esp_err_t WifiRadioStub::init() noexcept {
    initialized_ = true;
    ESP_LOGI(kTag, "stub initialized");
    return ESP_OK;
}

esp_err_t WifiRadioStub::start_ap(AccessPointConfig const& config) noexcept {
    if (!initialized_) {
        return ESP_ERR_INVALID_STATE;
    }
    ap_config_ = config;
    ap_running_ = true;
    ESP_LOGI(kTag, "SoftAP '%s' started (stub)", config.ssid.data());
    return ESP_OK;
}

esp_err_t WifiRadioStub::stop_ap() noexcept {
    if (!initialized_) {
        return ESP_ERR_INVALID_STATE;
    }
    ap_running_ = false;
    ESP_LOGI(kTag, "SoftAP stopped (stub)");
    return ESP_OK;
}

esp_err_t WifiRadioStub::start_station(StationConfig const& config) noexcept {
    if (!initialized_) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!config.has_ssid()) {
        return ESP_ERR_INVALID_ARG;
    }
    station_config_ = config;
    station_running_ = true;
    ESP_LOGI(kTag, "station connecting to '%s' (stub)", config.ssid.data());
    return ESP_OK;
}

esp_err_t WifiRadioStub::stop_station() noexcept {
    if (!initialized_) {
        return ESP_ERR_INVALID_STATE;
    }
    station_running_ = false;
    ESP_LOGI(kTag, "station stopped (stub)");
    return ESP_OK;
}

bool WifiRadioStub::initialized() const noexcept {
    return initialized_;
}

bool WifiRadioStub::ap_running() const noexcept {
    return ap_running_;
}

bool WifiRadioStub::station_running() const noexcept {
    return station_running_;
}

std::optional<AccessPointConfig> const& WifiRadioStub::ap_config()
    const noexcept {
    return ap_config_;
}

std::optional<StationConfig> const& WifiRadioStub::station_config()
    const noexcept {
    return station_config_;
}

}  // namespace thermo::hal
