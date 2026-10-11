// Power-source detection (§2.2, Rev 13).
#include "power_manager.hpp"

#include <cstdint>

#include "driver/gpio.h"
#include "driver/usb_serial_jtag.h"
#include "esp_log.h"

namespace thermo {
namespace {

constexpr const char* kTag = "power";

// VBUS-detect GPIO: high when the USB port is supplying 5 V, regardless of
// whether a data host is attached. Boards without a dedicated VBUS-sense pin
// leave this pulled low, in which case the USB source is inferred from the
// data lines alone.
constexpr gpio_num_t kUsbVbusDetect = GPIO_NUM_33;

// A reading at or above this level is treated as "the solar battery is
// installed and supplying the board"; below it the divider sees no source.
constexpr std::uint32_t kBatteryPresentMv = 3000;

}  // namespace

PowerManager::PowerManager(Config const& config)
    : config_(config), source_(detect()) {
    // Only the solar-battery path needs the (and can own) the ADC-backed
    // battery monitor. When running from USB there may be no battery at all,
    // so `battery_` stays empty.
    if (source_ == PowerSource::kSolarBattery) {
        battery_.emplace(config_);
    }
    ESP_LOGI(kTag, "power source: %s", to_string(source_));
}

PowerSource PowerManager::detect() const noexcept {
    if (solar_battery_installed()) {
        return PowerSource::kSolarBattery;
    }
    if (usb_host_connected()) {
        return PowerSource::kUsbComputer;
    }
    if (usb_vbus_present()) {
        return PowerSource::kUsbBatteryPack;
    }
    return PowerSource::kUnknown;
}

bool PowerManager::usb_host_connected() noexcept {
    // Ensure the driver is installed if not already used by the system console
    if (!usb_serial_jtag_is_driver_installed()) {
        usb_serial_jtag_driver_config_t cfg =
            USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&cfg));
    }

    // The ESP32-S3 USB-Serial-JTAG peripheral reports "connected" only once a
    // host has enumerated it; a charger/battery pack keeps it disconnected.
    return usb_serial_jtag_is_connected();
}

bool PowerManager::usb_vbus_present() noexcept {
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = 1ULL << kUsbVbusDetect;
    cfg.mode = GPIO_MODE_INPUT;
    cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_ENABLE;
    cfg.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&cfg);
    return gpio_get_level(kUsbVbusDetect) != 0;
}

bool PowerManager::solar_battery_installed() const noexcept {
    // Probe the divider with a throwaway monitor: an installed battery reads a
    // plausible voltage; an absent one floats near zero.
    BatteryMonitor probe(config_);
    const auto mv = probe.read_mv();
    return mv.has_value() && *mv >= kBatteryPresentMv;
}

/** If we are on battery, return the strength of the battery. */
std::optional<uint32_t> PowerManager::read_mv() noexcept {
    if (!battery_.has_value()) {
        return std::nullopt;
    }
    auto const mv = battery_.value().read_mv();
    return mv;
}

}  // namespace thermo
