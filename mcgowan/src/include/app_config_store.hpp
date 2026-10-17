// NVS-backed configuration persistence (§5.2).
#pragma once

#include <string>

#include "app_config.hpp"
#include "esp_err.h"

namespace thermo {

// Loads configuration from NVS into `out`, applying spec defaults for any key
// that is absent (fresh device). On a fresh device, factory-default NVS keys
// are written so the config page reflects them immediately.
esp_err_t config_load(Config& out);

// Persists every field of `cfg` to NVS under namespace "thermo_cfg".
esp_err_t config_save(const Config& cfg);

// Erases the configuration namespace (used for factory reset).
esp_err_t config_reset();

// Derives the factory-default AP SSID (ESP32-Thermo-XXXX) from the base MAC.
std::string default_ap_ssid();

}  // namespace thermo
