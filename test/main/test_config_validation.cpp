// Tests for configuration validation (§5.1, §5.3).
//
// Validation runs before anything is persisted to NVS (§5.3 step 2), so these
// rules are the gate that keeps a bad form submission out of the device's
// stored configuration.
#include "unity.h"

#include <string>

#include "app_config.hpp"

using thermo::Config;
using thermo::validation::validate;
using thermo::validation::validate_ap_password;
using thermo::validation::validate_device_name;
using thermo::validation::validate_interval;
using thermo::validation::validate_ntp_server;
using thermo::validation::validate_ssid;
using thermo::validation::validate_sta_password;
using thermo::validation::validate_url;

namespace {

// A configuration that passes every rule; individual tests perturb one field.
Config valid_config() {
    Config cfg;
    cfg.ap_ssid = "ThermoSetup";
    cfg.ap_pass = "setup1234";
    cfg.sta_ssid = "HomeNetwork";
    cfg.sta_pass = "hunter2hunter2";
    cfg.dest_url = "https://example.com/ingest";
    cfg.interval_s = 3600;
    cfg.retry_base_s = 60;
    cfg.retry_max_s = 3600;
    cfg.batch_size = 1;
    cfg.dev_name = "greenhouse-1";
    cfg.ntp_server = "pool.ntp.org";
    cfg.batt_low_mv = 3400;
    cfg.batt_high_mv = 3700;
    cfg.batt_r_top = 100000;
    cfg.batt_r_bot = 100000;
    cfg.batt_cal_gain = 1.0f;
    return cfg;
}

}  // namespace

// --- Baseline ---------------------------------------------------------------

TEST_CASE("validate: a well-formed config passes", "[validation]") {
    const auto result = validate(valid_config());
    TEST_ASSERT_TRUE(result.ok);
    TEST_ASSERT_EQUAL_INT(0, static_cast<int>(result.errors.size()));
}

// --- SSID -------------------------------------------------------------------

TEST_CASE("validate_ssid: length bounds", "[validation]") {
    TEST_ASSERT_TRUE(validate_ssid("").has_value());    // too short
    TEST_ASSERT_FALSE(validate_ssid("A").has_value());  // 1 char ok
    TEST_ASSERT_FALSE(validate_ssid(std::string(32, 'a')).has_value());
    TEST_ASSERT_TRUE(
        validate_ssid(std::string(33, 'a')).has_value());  // too long
}

TEST_CASE("validate_ssid: rejects control characters", "[validation]") {
    std::string with_tab = "bad\tssid";
    TEST_ASSERT_TRUE(validate_ssid(with_tab).has_value());
}

// --- Passwords --------------------------------------------------------------

TEST_CASE("validate_ap_password: 8-63 characters", "[validation]") {
    TEST_ASSERT_TRUE(validate_ap_password("short").has_value());  // 5 chars
    TEST_ASSERT_FALSE(validate_ap_password("12345678").has_value());
    TEST_ASSERT_FALSE(validate_ap_password(std::string(63, 'x')).has_value());
    TEST_ASSERT_TRUE(validate_ap_password(std::string(64, 'x')).has_value());
}

TEST_CASE("validate_sta_password: empty allowed (open network)",
          "[validation]") {
    TEST_ASSERT_FALSE(validate_sta_password("").has_value());
    // Non-empty must still obey the WPA2 bounds.
    TEST_ASSERT_TRUE(validate_sta_password("abc").has_value());
    TEST_ASSERT_FALSE(validate_sta_password("abcdefgh").has_value());
}

// --- URL --------------------------------------------------------------------

TEST_CASE("validate_url: requires an http(s) scheme and a host",
          "[validation]") {
    TEST_ASSERT_FALSE(validate_url("https://example.com/x").has_value());
    TEST_ASSERT_FALSE(validate_url("http://h/x").has_value());

    TEST_ASSERT_TRUE(validate_url("example.com/x").has_value());
    TEST_ASSERT_TRUE(validate_url("ftp://example.com").has_value());
    // Scheme present but no host.
    TEST_ASSERT_TRUE(validate_url("https://").has_value());
    TEST_ASSERT_TRUE(validate_url("http://").has_value());
}

// --- Interval ---------------------------------------------------------------

TEST_CASE("validate_interval: positive integers only", "[validation]") {
    TEST_ASSERT_FALSE(validate_interval("1").has_value());
    TEST_ASSERT_FALSE(validate_interval("3600").has_value());

    TEST_ASSERT_TRUE(validate_interval("0").has_value());
    TEST_ASSERT_TRUE(validate_interval("-5").has_value());
    TEST_ASSERT_TRUE(validate_interval("abc").has_value());
    TEST_ASSERT_TRUE(validate_interval("12abc").has_value());  // trailing junk
    TEST_ASSERT_TRUE(validate_interval("").has_value());
}

// --- Device name and NTP server --------------------------------------------

TEST_CASE("validate_device_name: 1-32 characters", "[validation]") {
    TEST_ASSERT_FALSE(validate_device_name("shed").has_value());
    TEST_ASSERT_TRUE(validate_device_name("").has_value());
    TEST_ASSERT_TRUE(validate_device_name(std::string(33, 'd')).has_value());
}

TEST_CASE("validate_ntp_server: hostname, no scheme", "[validation]") {
    TEST_ASSERT_FALSE(validate_ntp_server("pool.ntp.org").has_value());
    TEST_ASSERT_FALSE(validate_ntp_server("time-a.nist.gov").has_value());

    TEST_ASSERT_TRUE(validate_ntp_server("").has_value());
    TEST_ASSERT_TRUE(validate_ntp_server("https://pool.ntp.org").has_value());
    TEST_ASSERT_TRUE(validate_ntp_server("bad host").has_value());
}

// --- Cross-field rules ------------------------------------------------------

TEST_CASE("validate: high-water must exceed low-water", "[validation]") {
    Config cfg = valid_config();
    cfg.batt_high_mv = cfg.batt_low_mv;  // not strictly greater
    const auto result = validate(cfg);
    TEST_ASSERT_FALSE(result.ok);
    TEST_ASSERT_EQUAL_STRING("batt_high_mv", result.errors[0].field.c_str());
}

TEST_CASE("validate: retry_max must be >= retry_base", "[validation]") {
    Config cfg = valid_config();
    cfg.retry_base_s = 600;
    cfg.retry_max_s = 300;
    const auto result = validate(cfg);
    TEST_ASSERT_FALSE(result.ok);

    bool found = false;
    for (const auto& e : result.errors) {
        if (e.field == "retry_max_s") {
            found = true;
        }
    }
    TEST_ASSERT_TRUE(found);
}

TEST_CASE("validate: batch_size must be at least 1", "[validation]") {
    Config cfg = valid_config();
    cfg.batch_size = 0;
    TEST_ASSERT_FALSE(validate(cfg).ok);
}

TEST_CASE("validate: battery thresholds and resistors must be positive",
          "[validation]") {
    Config cfg = valid_config();
    cfg.batt_low_mv = 0;
    TEST_ASSERT_FALSE(validate(cfg).ok);

    cfg = valid_config();
    cfg.batt_r_top = 0;
    TEST_ASSERT_FALSE(validate(cfg).ok);

    cfg = valid_config();
    cfg.batt_r_bot = 0;
    TEST_ASSERT_FALSE(validate(cfg).ok);

    cfg = valid_config();
    cfg.batt_cal_gain = 0.0f;
    TEST_ASSERT_FALSE(validate(cfg).ok);
}

TEST_CASE("validate: retry_base of zero is rejected", "[validation]") {
    Config cfg = valid_config();
    cfg.retry_base_s = 0;
    cfg.retry_max_s = 3600;  // keep max >= base so only the base rule fires
    TEST_ASSERT_FALSE(validate(cfg).ok);
}

TEST_CASE("validate: accumulates multiple errors", "[validation]") {
    Config cfg = valid_config();
    cfg.ap_ssid = "";
    cfg.dest_url = "ftp://nope";
    cfg.batch_size = 0;
    const auto result = validate(cfg);
    TEST_ASSERT_FALSE(result.ok);
    TEST_ASSERT_TRUE(result.errors.size() >= 3);
}
