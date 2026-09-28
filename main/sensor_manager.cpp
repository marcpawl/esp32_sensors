// High-level sensor manager binding the 1-Wire bus and DS18B20 devices.
#include "sensor.hpp"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace thermo {
namespace {

constexpr const char* kTag = "sensors";

// DS18B20 conversion time at 12-bit resolution is ~750 ms.
constexpr int kConversionWaitMs = 800;

}  // namespace

SensorManager::SensorManager(int gpio) : bus_(gpio), device_(bus_) {}

esp_err_t SensorManager::init() {
    return bus_.init();
}

std::vector<SensorReading> SensorManager::read_all(
    const std::vector<RomId>& roms, std::vector<RomId>* out_roms) {
    std::vector<RomId> active = roms;

    if (active.empty()) {
        // No cached ROM IDs: scan. Run mode avoids this by caching ROM IDs in
        // RTC memory (§3.4), so the slow search only runs on first boot.
        ESP_LOGW(kTag, "cached ROM list empty; scanning bus");
        active = device_.scan();
        if (out_roms) {
            *out_roms = active;
        }
    }

    // Kick off one conversion for all devices, then wait once.
    if (device_.start_conversion() != ESP_OK) {
        ESP_LOGW(kTag, "no presence pulse; all readings null");
        std::vector<SensorReading> nulls;
        nulls.reserve(active.size());
        for (const auto& rom : active) {
            nulls.push_back({rom, std::nullopt});
        }
        return nulls;
    }
    vTaskDelay(pdMS_TO_TICKS(kConversionWaitMs));

    std::vector<SensorReading> readings;
    readings.reserve(active.size());
    for (const auto& rom : active) {
        readings.push_back({rom, device_.read_temperature(rom)});
    }
    return readings;
}

}  // namespace thermo
