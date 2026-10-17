// Configuration validation rules (§5.1). Pure logic, no I/O.
#include "app_config.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <string_view>

namespace thermo::validation {
namespace {

bool all_alnum_etc(std::string_view v) {
    // Reject control characters and whitespace for credentials/SSIDs; allow a
    // broad set of printable ASCII.
    return std::all_of(v.begin(), v.end(),
                       [](unsigned char c) { return c >= 0x20 && c < 0x7F; });
}

std::optional<std::string> check_length(std::string_view v, std::size_t lo,
                                        std::size_t hi, const char* what) {
    if (v.size() < lo || v.size() > hi) {
        return std::string(what) + " must be " + std::to_string(lo) + "-" +
               std::to_string(hi) + " characters";
    }
    if (!all_alnum_etc(v)) {
        return std::string(what) + " contains invalid characters";
    }
    return std::nullopt;
}

std::optional<std::uint32_t> parse_u32(std::string_view v) {
    std::uint32_t value = 0;
    const auto* begin = v.data();
    const auto* end = v.data() + v.size();
    auto [ptr, ec] = std::from_chars(begin, end, value);
    if (ec != std::errc{} || ptr != end) {
        return std::nullopt;
    }
    return value;
}

}  // namespace

std::optional<std::string> validate_ssid(const std::string& v) {
    return check_length(v, 1, 32, "SSID");
}

std::optional<std::string> validate_ap_password(const std::string& v) {
    return check_length(v, 8, 63, "Password");
}

std::optional<std::string> validate_sta_password(const std::string& v) {
    // Empty is allowed: an open network has no password.
    if (v.empty()) {
        return std::nullopt;
    }
    return check_length(v, 8, 63, "Station password");
}

std::optional<std::string> validate_url(const std::string& v) {
    if (v.rfind("http://", 0) != 0 && v.rfind("https://", 0) != 0) {
        return std::string("URL must start with http:// or https://");
    }
    if (v.size() <= 8) {
        return std::string("URL must include a host");
    }
    return std::nullopt;
}

std::optional<std::string> validate_interval(std::string_view v) {
    const auto parsed = parse_u32(v);
    if (!parsed || *parsed == 0) {
        return std::string("must be a positive integer");
    }
    return std::nullopt;
}

std::optional<std::string> validate_device_name(const std::string& v) {
    return check_length(v, 1, 32, "Device name");
}

std::optional<std::string> validate_ntp_server(const std::string& v) {
    if (v.empty()) {
        return std::string("NTP server must not be empty");
    }
    if (v.find("://") != std::string::npos) {
        return std::string("NTP server must be a hostname without a scheme");
    }
    if (v.size() > 253) {
        return std::string("NTP server hostname is too long");
    }
    const bool valid = std::all_of(v.begin(), v.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '.' || c == '-' || c == '_';
    });
    if (!valid) {
        return std::string("NTP server contains invalid characters");
    }
    return std::nullopt;
}

ValidationResult validate(const Config& cfg) {
    ValidationResult result{};
    auto add = [&result](const char* field, std::optional<std::string> msg) {
        if (msg) {
            result.errors.push_back({field, *msg});
        }
    };

    add("ap_ssid", validate_ssid(cfg.ap_ssid));
    add("ap_pass", validate_ap_password(cfg.ap_pass));
    add("sta_ssid", validate_ssid(cfg.sta_ssid));
    add("sta_pass", validate_sta_password(cfg.sta_pass));
    add("dest_url", validate_url(cfg.dest_url));
    add("interval_s", validate_interval(std::to_string(cfg.interval_s)));
    add("dev_name", validate_device_name(cfg.dev_name));
    add("ntp_server", validate_ntp_server(cfg.ntp_server));

    if (cfg.retry_base_s == 0) {
        result.errors.push_back({"retry_base_s", "must be > 0"});
    }
    if (cfg.retry_max_s < cfg.retry_base_s) {
        result.errors.push_back(
            {"retry_max_s", "must be >= the base retry interval"});
    }
    if (cfg.batch_size < 1) {
        result.errors.push_back({"batch_size", "must be >= 1"});
    }
    if (cfg.batt_low_mv == 0) {
        result.errors.push_back({"batt_low_mv", "must be > 0"});
    }
    if (cfg.batt_high_mv <= cfg.batt_low_mv) {
        result.errors.push_back(
            {"batt_high_mv", "must be greater than the low-water threshold"});
    }
    if (cfg.batt_r_top == 0) {
        result.errors.push_back({"batt_r_top", "must be > 0"});
    }
    if (cfg.batt_r_bot == 0) {
        result.errors.push_back({"batt_r_bot", "must be > 0"});
    }
    if (cfg.batt_cal_gain <= 0.0f) {
        result.errors.push_back({"batt_cal_gain", "must be > 0"});
    }

    result.ok = result.errors.empty();
    return result;
}

}  // namespace thermo::validation
