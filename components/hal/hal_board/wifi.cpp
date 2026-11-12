// Real WiFi radio implementation for the dev board (§3.2, §3.9). Drives the
// ESP-IDF WiFi driver through a fixed sequence: stop the driver, set the mode
// for the enabled interfaces, restart it, then apply the pending interface
// configuration (and connect the station).
// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#pragma GCC diagnostic pop
// NOLINTEND

#include <cstdint>
#include <cstring>

#include "hal/wifi.hpp"

namespace thermo::hal {
namespace {

constexpr const char* kTag = "hal_wifi";

// The vocabulary buffers must hold the driver's fields (plus the NUL for the
// SSID), or the copies below would truncate.
static_assert(sizeof(AccessPointConfig{}.ssid) >= 32 + 1);
static_assert(sizeof(AccessPointConfig{}.password) >= 64);
static_assert(sizeof(StationConfig{}.ssid) >= 32);
static_assert(sizeof(StationConfig{}.password) >= 64);

bool g_stack_initialized = false;

wifi_auth_mode_t ap_auth_mode(AccessPointConfig const& config) {
    return config.password[0] == '\0' ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
}

// Fills the driver's AP configuration from the HAL description.
void make_ap_config(AccessPointConfig const& config, wifi_config_t& out) {
    out = {};
    std::strncpy(reinterpret_cast<char*>(out.ap.ssid), config.ssid.data(),
                 sizeof(out.ap.ssid) - 1);
    out.ap.ssid_len =
        static_cast<std::uint8_t>(std::strlen(config.ssid.data()));
    std::strncpy(reinterpret_cast<char*>(out.ap.password),
                 config.password.data(), sizeof(out.ap.password) - 1);
    // An empty password means an open network; otherwise WPA2-PSK (the
    // driver picks CCMP).
    out.ap.authmode = ap_auth_mode(config);
    out.ap.channel = config.channel;
    out.ap.max_connection = config.max_connections;
    out.ap.ssid_hidden = config.hidden ? 1 : 0;
}

// Fills the driver's station configuration from the HAL description. The
// scan threshold is left unset so the driver infers the weakest acceptable
// auth mode from the password, keeping WPA/WPA2 and WPA2/WPA3 networks
// connectable.
void make_station_config(StationConfig const& config, wifi_config_t& out) {
    out = {};
    std::strncpy(reinterpret_cast<char*>(out.sta.ssid), config.ssid.data(),
                 sizeof(out.sta.ssid) - 1);
    std::strncpy(reinterpret_cast<char*>(out.sta.password),
                 config.password.data(), sizeof(out.sta.password) - 1);
}

// Stops the driver if it is running. A driver that was never started (or is
// already stopping) is not an error.
esp_err_t stop_driver() {
    esp_err_t const err = esp_wifi_stop();
    if (err == ESP_OK || err == ESP_ERR_WIFI_NOT_STARTED) {
        return ESP_OK;
    }
    return err;
}

}  // namespace

esp_err_t WifiRadioBoard::init() noexcept {
    if (!g_stack_initialized) {
        esp_err_t err = esp_netif_init();
        if (err != ESP_OK) {
            ESP_LOGE(kTag, "esp_netif_init failed: %s", esp_err_to_name(err));
            return err;
        }
        err = esp_event_loop_create_default();
        if (err != ESP_OK) {
            ESP_LOGE(kTag, "event loop init failed: %s", esp_err_to_name(err));
            return err;
        }
        g_stack_initialized = true;
    }
    if (!initialized_) {
        if (esp_netif_create_default_wifi_ap() == nullptr ||
            esp_netif_create_default_wifi_sta() == nullptr) {
            ESP_LOGE(kTag, "failed to create default WiFi interfaces");
            return ESP_FAIL;
        }
        // The driver starts in WIFI_MODE_NULL (no interface running) and is
        // left stopped until an interface is enabled.
        wifi_init_config_t const init_config = WIFI_INIT_CONFIG_DEFAULT();
        esp_err_t const err = esp_wifi_init(&init_config);
        if (err != ESP_OK) {
            ESP_LOGE(kTag, "esp_wifi_init failed: %s", esp_err_to_name(err));
            return err;
        }
        initialized_ = true;
    }
    return ESP_OK;
}

esp_err_t WifiRadioBoard::start_ap(AccessPointConfig const& config) noexcept {
    if (!initialized_) {
        return ESP_ERR_INVALID_STATE;
    }
    wifi_config_t wifi_config;
    make_ap_config(config, wifi_config);
    ap_enabled_ = true;
    esp_err_t err = apply_interfaces();
    if (err != ESP_OK) {
        return err;
    }
    err = esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "AP config failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(kTag, "SoftAP '%s' started", config.ssid.data());
    return ESP_OK;
}

esp_err_t WifiRadioBoard::stop_ap() noexcept {
    if (!initialized_) {
        return ESP_ERR_INVALID_STATE;
    }
    if (ap_enabled_) {
        ap_enabled_ = false;
        esp_err_t const err = apply_interfaces();
        if (err != ESP_OK) {
            return err;
        }
        ESP_LOGI(kTag, "SoftAP stopped");
    }
    return ESP_OK;
}

esp_err_t WifiRadioBoard::start_station(StationConfig const& config) noexcept {
    if (!initialized_) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!config.has_ssid()) {
        return ESP_ERR_INVALID_ARG;
    }
    wifi_config_t wifi_config;
    make_station_config(config, wifi_config);
    station_enabled_ = true;
    esp_err_t err = apply_interfaces();
    if (err != ESP_OK) {
        return err;
    }
    err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "station config failed: %s", esp_err_to_name(err));
        return err;
    }
    // Disconnect first so a restart is deterministic: the driver drops any
    // current association and reconnects using the configuration just set.
    (void)esp_wifi_disconnect();
    err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "station connect failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(kTag, "station connecting to '%s'", config.ssid.data());
    return ESP_OK;
}

esp_err_t WifiRadioBoard::stop_station() noexcept {
    if (!initialized_) {
        return ESP_ERR_INVALID_STATE;
    }
    if (station_enabled_) {
        (void)esp_wifi_disconnect();
        station_enabled_ = false;
        esp_err_t const err = apply_interfaces();
        if (err != ESP_OK) {
            return err;
        }
        ESP_LOGI(kTag, "station stopped");
    }
    return ESP_OK;
}

esp_err_t WifiRadioBoard::apply_interfaces() noexcept {
    // The driver only accepts a mode change while stopped.
    esp_err_t err = stop_driver();
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "esp_wifi_stop failed: %s", esp_err_to_name(err));
        return err;
    }

    // WIFI_MODE_NULL runs no interface: an idle radio draws no WiFi power.
    wifi_mode_t mode = WIFI_MODE_NULL;
    if (ap_enabled_ && station_enabled_) {
        mode = WIFI_MODE_APSTA;
    } else if (ap_enabled_) {
        mode = WIFI_MODE_AP;
    } else if (station_enabled_) {
        mode = WIFI_MODE_STA;
    }
    err = esp_wifi_set_mode(mode);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "esp_wifi_set_mode failed: %s", esp_err_to_name(err));
        return err;
    }
    if (mode == WIFI_MODE_NULL) {
        return ESP_OK;
    }
    err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "esp_wifi_start failed: %s", esp_err_to_name(err));
        return err;
    }
    return ESP_OK;
}

}  // namespace thermo::hal
