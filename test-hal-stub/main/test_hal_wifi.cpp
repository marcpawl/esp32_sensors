// Tests for the stub WiFi radio (`hal_stub`, §3.2, §3.9).
//
// The radio HAL is deliberately semantics-free: a no-argument `create()`
// factory, `init()`, and an independent start/stop pair for the access point
// and for the station. These tests cover the *stub device* contract — the
// `WifiRadioLike` concept, `init()` gating, start/stop independence, the
// recorded configuration, and the empty-SSID rejection — plus the
// `<hal/wifi.hpp>` `WifiRadio` alias. They run on the ESP-IDF `linux` target
// with Unity and link the pin-free `hal_stub` component (never `hal_board`).
//
// The stub holds its state in software, so every case is deterministic and
// needs no radio.
// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#pragma GCC diagnostic pop
// NOLINTEND

#include <concepts>

#include "hal/wifi.hpp"
#include "hal/wifi_types.hpp"
#include "unity.h"

namespace {

// The radio implementation linked into this project (the stub here).
using Radio = thermo::hal::WifiRadio;

using thermo::hal::AccessPointConfig;
using thermo::hal::copy_text;
using thermo::hal::StationConfig;

AccessPointConfig make_ap_config() {
    AccessPointConfig config;
    copy_text(config.ssid, "ESP32-Thermo-TEST");
    copy_text(config.password, "configme123");
    config.channel = 6;
    config.max_connections = 2;
    config.hidden = false;
    return config;
}

StationConfig make_station_config() {
    StationConfig config;
    copy_text(config.ssid, "home-net");
    copy_text(config.password, "hunter2hunter2");
    return config;
}

// A radio that has been through init(), the only state in which the
// start/stop methods are allowed.
Radio make_radio() {
    Radio radio = Radio::create();
    radio.init();
    return radio;
}

}  // namespace

// --- Concept / alias --------------------------------------------------------

TEST_CASE("hal wifi: WifiRadioStub models WifiRadioLike", "[hal_wifi]") {
    static_assert(thermo::hal::WifiRadioLike<thermo::hal::WifiRadioStub>);
}

TEST_CASE("hal wifi: WifiRadio alias is the stub in this project",
          "[hal_wifi]") {
    static_assert(std::same_as<Radio, thermo::hal::WifiRadioStub>);
    static_assert(std::same_as<decltype(Radio::create()), Radio>);
}

// --- Construction / init gating --------------------------------------------

TEST_CASE("hal wifi: init() succeeds and is idempotent", "[hal_wifi]") {
    Radio radio = Radio::create();
    TEST_ASSERT_FALSE(radio.initialized());
    TEST_ASSERT_EQUAL(ESP_OK, radio.init());
    TEST_ASSERT_TRUE(radio.initialized());
    TEST_ASSERT_EQUAL(ESP_OK, radio.init());
}

TEST_CASE("hal wifi: methods before init() return ESP_ERR_INVALID_STATE",
          "[hal_wifi]") {
    Radio radio = Radio::create();
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, radio.start_ap(make_ap_config()));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, radio.stop_ap());
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
                      radio.start_station(make_station_config()));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, radio.stop_station());
    TEST_ASSERT_FALSE(radio.ap_running());
    TEST_ASSERT_FALSE(radio.station_running());
}

// --- Access point -----------------------------------------------------------

TEST_CASE("hal wifi: start_ap() brings the AP up and records its config",
          "[hal_wifi]") {
    Radio radio = make_radio();
    AccessPointConfig const config = make_ap_config();
    TEST_ASSERT_EQUAL(ESP_OK, radio.start_ap(config));
    TEST_ASSERT_TRUE(radio.ap_running());
    TEST_ASSERT_TRUE(radio.ap_config().has_value());
    TEST_ASSERT_TRUE(radio.ap_config().value() == config);
    TEST_ASSERT_FALSE(radio.station_running());
}

TEST_CASE("hal wifi: restarting the AP replaces its configuration",
          "[hal_wifi]") {
    Radio radio = make_radio();
    TEST_ASSERT_EQUAL(ESP_OK, radio.start_ap(make_ap_config()));

    AccessPointConfig renamed = make_ap_config();
    copy_text(renamed.ssid, "ESP32-Thermo-NEW");
    TEST_ASSERT_EQUAL(ESP_OK, radio.start_ap(renamed));

    TEST_ASSERT_TRUE(radio.ap_running());
    TEST_ASSERT_EQUAL_STRING("ESP32-Thermo-NEW",
                             radio.ap_config()->ssid.data());
}

TEST_CASE("hal wifi: stop_ap() succeeds whether or not the AP is up",
          "[hal_wifi]") {
    Radio radio = make_radio();
    TEST_ASSERT_EQUAL(ESP_OK, radio.stop_ap());  // never started
    TEST_ASSERT_FALSE(radio.ap_running());

    TEST_ASSERT_EQUAL(ESP_OK, radio.start_ap(make_ap_config()));
    TEST_ASSERT_EQUAL(ESP_OK, radio.stop_ap());
    TEST_ASSERT_FALSE(radio.ap_running());

    // The recorded configuration survives the stop.
    TEST_ASSERT_TRUE(radio.ap_config().has_value());
}

// --- Station ----------------------------------------------------------------

TEST_CASE(
    "hal wifi: start_station() brings the station up and records its "
    "config",
    "[hal_wifi]") {
    Radio radio = make_radio();
    StationConfig const config = make_station_config();
    TEST_ASSERT_EQUAL(ESP_OK, radio.start_station(config));
    TEST_ASSERT_TRUE(radio.station_running());
    TEST_ASSERT_TRUE(radio.station_config().has_value());
    TEST_ASSERT_TRUE(radio.station_config().value() == config);
    TEST_ASSERT_FALSE(radio.ap_running());
}

TEST_CASE("hal wifi: start_station() rejects an empty SSID", "[hal_wifi]") {
    Radio radio = make_radio();
    StationConfig const empty;
    TEST_ASSERT_FALSE(empty.has_ssid());

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, radio.start_station(empty));
    TEST_ASSERT_FALSE(radio.station_running());
    TEST_ASSERT_FALSE(radio.station_config().has_value());  // unchanged
}

TEST_CASE("hal wifi: retrying the station join re-records the config",
          "[hal_wifi]") {
    // Configure mode retries periodically (§3.2); the stub simply records the
    // most recent request.
    Radio radio = make_radio();
    TEST_ASSERT_EQUAL(ESP_OK, radio.start_station(make_station_config()));
    TEST_ASSERT_EQUAL(ESP_OK, radio.start_station(make_station_config()));
    TEST_ASSERT_TRUE(radio.station_running());
}

TEST_CASE("hal wifi: stop_station() succeeds whether or not it is up",
          "[hal_wifi]") {
    Radio radio = make_radio();
    TEST_ASSERT_EQUAL(ESP_OK, radio.stop_station());  // never started

    TEST_ASSERT_EQUAL(ESP_OK, radio.start_station(make_station_config()));
    TEST_ASSERT_EQUAL(ESP_OK, radio.stop_station());
    TEST_ASSERT_FALSE(radio.station_running());
    TEST_ASSERT_TRUE(radio.station_config().has_value());
}

// --- Independence -----------------------------------------------------------

TEST_CASE("hal wifi: the AP and the station run at the same time",
          "[hal_wifi]") {
    // Configure mode (§3.2) serves the config page over the AP while trying
    // to reach the configured network as a station.
    Radio radio = make_radio();
    TEST_ASSERT_EQUAL(ESP_OK, radio.start_ap(make_ap_config()));
    TEST_ASSERT_EQUAL(ESP_OK, radio.start_station(make_station_config()));
    TEST_ASSERT_TRUE(radio.ap_running());
    TEST_ASSERT_TRUE(radio.station_running());
}

TEST_CASE("hal wifi: stopping one interface leaves the other running",
          "[hal_wifi]") {
    Radio radio = make_radio();
    TEST_ASSERT_EQUAL(ESP_OK, radio.start_ap(make_ap_config()));
    TEST_ASSERT_EQUAL(ESP_OK, radio.start_station(make_station_config()));

    TEST_ASSERT_EQUAL(ESP_OK, radio.stop_station());
    TEST_ASSERT_TRUE(radio.ap_running());
    TEST_ASSERT_FALSE(radio.station_running());

    TEST_ASSERT_EQUAL(ESP_OK, radio.start_station(make_station_config()));
    TEST_ASSERT_EQUAL(ESP_OK, radio.stop_ap());
    TEST_ASSERT_FALSE(radio.ap_running());
    TEST_ASSERT_TRUE(radio.station_running());
}
