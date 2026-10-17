// Wi-Fi Access Point bring-up for Configure mode (§3.2).
#include "wifi_ap.hpp"

#include <cstring>

#include "constants.hpp"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "esp_wifi.h"

namespace thermo {
namespace {

constexpr const char* kTag = "wifi_ap";

bool g_stack_initialized = false;
esp_netif_t* g_ap_netif = nullptr;

void ensure_netif_and_events() {
    if (!g_stack_initialized) {
        ESP_ERROR_CHECK(esp_netif_init());
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        g_stack_initialized = true;
    }
    if (g_ap_netif == nullptr) {
        g_ap_netif = esp_netif_create_default_wifi_ap();
    }
}

}  // namespace

esp_err_t WifiAp::start(const Config& cfg) {
    ensure_netif_and_events();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_wifi_init(&init_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "esp_wifi_init failed: %s", esp_err_to_name(err));
        return err;
    }

    wifi_config_t wifi_cfg = {};
    // Copy the configured SSID/password into the fixed-size Wi-Fi structs.
    std::strncpy(reinterpret_cast<char*>(wifi_cfg.ap.ssid), cfg.ap_ssid.c_str(),
                 sizeof(wifi_cfg.ap.ssid) - 1);
    wifi_cfg.ap.ssid_len = static_cast<std::uint8_t>(cfg.ap_ssid.size());
    std::strncpy(reinterpret_cast<char*>(wifi_cfg.ap.password),
                 cfg.ap_pass.c_str(), sizeof(wifi_cfg.ap.password) - 1);
    wifi_cfg.ap.max_connection = 4;
    wifi_cfg.ap.authmode =
        cfg.ap_pass.empty() ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    wifi_cfg.ap.channel = 1;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_cfg));
    err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "esp_wifi_start failed: %s", esp_err_to_name(err));
        return err;
    }
    started_ = true;
    ESP_LOGI(kTag, "SoftAP '%s' started", cfg.ap_ssid.c_str());
    return ESP_OK;
}

std::string WifiAp::ip_address() const {
    if (g_ap_netif == nullptr) {
        return "192.168.4.1";
    }
    esp_netif_ip_info_t ip{};
    if (esp_netif_get_ip_info(g_ap_netif, &ip) != ESP_OK) {
        return "192.168.4.1";
    }
    char buf[16];
    std::snprintf(buf, sizeof(buf), IPSTR, IP2STR(&ip.ip));
    return buf;
}

}  // namespace thermo
