// NVS-backed configuration persistence (§5.2).
#include "app_config_store.hpp"

#include <cstdio>
#include <cstring>

#include "constants.hpp"
#include "esp_log.h"
#include "esp_mac.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace thermo {
namespace {

constexpr const char* kTag = "config";

// NVS key names (§5.2).
constexpr const char* kApSsid = "ap_ssid";
constexpr const char* kApPass = "ap_pass";
constexpr const char* kStaSsid = "sta_ssid";
constexpr const char* kStaPass = "sta_pass";
constexpr const char* kDestUrl = "dest_url";
constexpr const char* kIntervalS = "interval_s";
constexpr const char* kRetryBaseS = "retry_base_s";
constexpr const char* kRetryMaxS = "retry_max_s";
constexpr const char* kBatchSize = "batch_size";
constexpr const char* kBattLowMv = "batt_low_mv";
constexpr const char* kBattHighMv = "batt_high_mv";
constexpr const char* kBattRTop = "batt_r_top";
constexpr const char* kBattRBot = "batt_r_bot";
constexpr const char* kBattCalGain = "batt_cal_gain";
constexpr const char* kBattCalOffset = "batt_cal_offset";
constexpr const char* kBattGateEn = "batt_gate_en";
constexpr const char* kDevName = "dev_name";
constexpr const char* kNtpServer = "ntp_server";
constexpr const char* kSavedTime = "saved_time";

std::string read_str(nvs_handle_t h, const char* key, const std::string& def) {
    std::size_t len = 0;
    if (nvs_get_str(h, key, nullptr, &len) != ESP_OK || len == 0) {
        return def;
    }
    std::string out(len, '\0');
    if (nvs_get_str(h, key, out.data(), &len) != ESP_OK) {
        return def;
    }
    // nvs_get_str writes a trailing NUL inside len; drop it.
    if (!out.empty() && out.back() == '\0') {
        out.pop_back();
    }
    return out;
}

std::uint32_t read_u32(nvs_handle_t h, const char* key, std::uint32_t def) {
    std::uint32_t v = def;
    nvs_get_u32(h, key, &v);
    return v;
}

std::int32_t read_i32(nvs_handle_t h, const char* key, std::int32_t def) {
    std::int32_t v = def;
    nvs_get_i32(h, key, &v);
    return v;
}

float read_f32(nvs_handle_t h, const char* key, float def) {
    // NVS has no float type; store as the raw bit pattern via a blob.
    float v = def;
    std::size_t len = sizeof(v);
    nvs_get_blob(h, key, &v, &len);
    return v;
}

std::uint8_t read_u8(nvs_handle_t h, const char* key, std::uint8_t def) {
    std::uint8_t v = def;
    nvs_get_u8(h, key, &v);
    return v;
}

// Enumerates `map_<ROMID>` keys and loads them into cfg.mappings.
void load_mappings(nvs_handle_t h, Config& cfg) {
    nvs_iterator_t it = nullptr;
    esp_err_t err =
        nvs_entry_find("nvs", config::kNamespace, NVS_TYPE_STR, &it);
    while (err == ESP_OK && it != nullptr) {
        nvs_entry_info_t info{};
        nvs_entry_info(it, &info);
        const std::string key = info.key;
        if (key.rfind("map_", 0) == 0) {
            SensorMapping m;
            m.rom = key.substr(4);
            m.name = read_str(h, info.key, "");
            if (!m.name.empty()) {
                cfg.mappings.push_back(std::move(m));
            }
        }
        err = nvs_entry_next(&it);
    }
    if (it) {
        nvs_release_iterator(it);
    }
}

}  // namespace

std::string default_ap_ssid() {
    std::uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "ESP32-Thermo-%02X%02X", mac[4], mac[5]);
    return buf;
}

esp_err_t config_load(Config& out) {
    nvs_handle_t h = 0;
    esp_err_t err = nvs_open(config::kNamespace, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        // Fresh device: apply defaults.
        out.ap_ssid = default_ap_ssid();
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    out.ap_ssid = read_str(h, kApSsid, default_ap_ssid());
    out.ap_pass = read_str(h, kApPass, "configme123");
    out.sta_ssid = read_str(h, kStaSsid, "");
    out.sta_pass = read_str(h, kStaPass, "");
    out.dest_url = read_str(h, kDestUrl, "");
    out.interval_s = read_u32(h, kIntervalS, 3600);
    out.retry_base_s = read_u32(h, kRetryBaseS, 60);
    out.retry_max_s = read_u32(h, kRetryMaxS, 3600);
    out.batch_size = read_u32(h, kBatchSize, 1);
    out.batt_low_mv = read_u32(h, kBattLowMv, 3400);
    out.batt_high_mv = read_u32(h, kBattHighMv, 3700);
    out.batt_r_top = read_u32(h, kBattRTop, 100000);
    out.batt_r_bot = read_u32(h, kBattRBot, 100000);
    out.batt_cal_gain = read_f32(h, kBattCalGain, 1.0f);
    out.batt_cal_offset = read_i32(h, kBattCalOffset, 0);
    out.batt_gate_en = read_u8(h, kBattGateEn, 0) != 0;
    out.dev_name = read_str(h, kDevName, "esp32-thermo");
    out.ntp_server = read_str(h, kNtpServer, "time.nrc.ca");
    out.saved_time = read_u32(h, kSavedTime, 0);

    load_mappings(h, out);
    nvs_close(h);
    return ESP_OK;
}

esp_err_t config_save(const Config& cfg) {
    nvs_handle_t h = 0;
    esp_err_t err = nvs_open(config::kNamespace, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "nvs_open failed: %s", esp_err_to_name(err));
        return err;
    }

    nvs_set_str(h, kApSsid, cfg.ap_ssid.c_str());
    nvs_set_str(h, kApPass, cfg.ap_pass.c_str());
    nvs_set_str(h, kStaSsid, cfg.sta_ssid.c_str());
    nvs_set_str(h, kStaPass, cfg.sta_pass.c_str());
    nvs_set_str(h, kDestUrl, cfg.dest_url.c_str());
    nvs_set_u32(h, kIntervalS, cfg.interval_s);
    nvs_set_u32(h, kRetryBaseS, cfg.retry_base_s);
    nvs_set_u32(h, kRetryMaxS, cfg.retry_max_s);
    nvs_set_u32(h, kBatchSize, cfg.batch_size);
    nvs_set_u32(h, kBattLowMv, cfg.batt_low_mv);
    nvs_set_u32(h, kBattHighMv, cfg.batt_high_mv);
    nvs_set_u32(h, kBattRTop, cfg.batt_r_top);
    nvs_set_u32(h, kBattRBot, cfg.batt_r_bot);
    nvs_set_blob(h, kBattCalGain, &cfg.batt_cal_gain,
                 sizeof(cfg.batt_cal_gain));
    nvs_set_i32(h, kBattCalOffset, cfg.batt_cal_offset);
    nvs_set_u8(h, kBattGateEn, cfg.batt_gate_en ? 1 : 0);
    nvs_set_str(h, kDevName, cfg.dev_name.c_str());
    nvs_set_str(h, kNtpServer, cfg.ntp_server.c_str());
    nvs_set_u32(h, kSavedTime, cfg.saved_time);

    for (const auto& m : cfg.mappings) {
        const std::string key = "map_" + m.rom;
        nvs_set_str(h, key.c_str(), m.name.c_str());
    }

    err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t config_reset() {
    nvs_handle_t h = 0;
    esp_err_t err = nvs_open(config::kNamespace, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_erase_all(h);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

}  // namespace thermo
