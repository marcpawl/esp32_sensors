// In-memory representation of the persisted configuration (§5) and the
// pure validation rules applied before it is written to NVS.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "constants.hpp"

namespace thermo {

// Per-sensor friendly-name mapping (§5.1.3).
struct SensorMapping {
    std::string rom;   // 16 hex chars, uppercase.
    std::string name;  // 1-32 chars.
};

struct Config {
    // §5.1.1 Network & Upload.
    std::string ap_ssid;
    std::string ap_pass;
    std::string sta_ssid;
    std::string sta_pass;
    std::string dest_url;
    std::uint32_t interval_s = 3600;
    std::uint32_t retry_base_s = 60;
    std::uint32_t retry_max_s = 3600;
    std::uint32_t batch_size = 1;
    std::string dev_name;
    std::string ntp_server;
    std::uint32_t saved_time = 0;  // Unix epoch UTC; 0 == "never".

    // §5.1.2 Battery thresholds & calibration.
    std::uint32_t batt_low_mv = 3400;
    std::uint32_t batt_high_mv = 3700;
    std::uint32_t batt_r_top = 100000;
    std::uint32_t batt_r_bot = 100000;
    float batt_cal_gain = 1.0f;
    std::int32_t batt_cal_offset = 0;
    bool batt_gate_en = false;

    // §5.1.3 / §5.2 map_<ROMID> friendly names.
    std::vector<SensorMapping> mappings;
};

// A single field-level validation failure, surfaced back to the config page.
struct ValidationError {
    std::string field;
    std::string message;
};

// Result of validating a submitted form. On failure, `errors` explains why and
// `config` must be treated as invalid (§5.3 step 2).
struct ValidationResult {
    bool ok = false;
    std::vector<ValidationError> errors;
};

namespace validation {

// Field-level validators (pure; no I/O). Each returns std::nullopt when the
// value is acceptable, otherwise a human-readable message.
std::optional<std::string> validate_ssid(const std::string& v);
std::optional<std::string> validate_ap_password(const std::string& v);
std::optional<std::string> validate_sta_password(const std::string& v);
std::optional<std::string> validate_url(const std::string& v);
std::optional<std::string> validate_interval(std::string_view v);
std::optional<std::string> validate_device_name(const std::string& v);
std::optional<std::string> validate_ntp_server(const std::string& v);

// Cross-field rules: high-water > low-water, retry max >= base, etc.
ValidationResult validate(const Config& cfg);

}  // namespace validation

}  // namespace thermo
