// Real ESP32 chip-temperature implementation for the dev board (§2.1, §10).
//
// Backed by the ESP-IDF internal temperature sensor driver
// (`esp_driver_tsens`): install the driver in `init()`, then read the die
// temperature in degrees Celsius and convert to hundredths of a degree.
#include "hal/chip_temperature.hpp"

#include <cassert>
#include <cmath>

#include "driver/temperature_sensor.h"

namespace thermo::hal {
namespace {

// The internal sensor is usable roughly over -10..80 °C on most ESP32 parts;
// a slightly wider window avoids "boundary cannot meet the range" warnings
// while covering the silicon's real operating range.
constexpr int kRangeMinC = -10;
constexpr int kRangeMaxC = 80;

// Converts a float Celsius reading to hundredths, rounded to nearest.
std::int16_t to_x100(float celsius) noexcept {
    return static_cast<std::int16_t>(std::lround(celsius * 100.0f));
}

}  // namespace

ChipTemperature::ChipTemperature() noexcept = default;

ChipTemperature::~ChipTemperature() noexcept {
    if (sensor_ != nullptr) {
        temperature_sensor_disable(
            static_cast<temperature_sensor_handle_t>(sensor_));
        temperature_sensor_uninstall(
            static_cast<temperature_sensor_handle_t>(sensor_));
        sensor_ = nullptr;
    }
}

esp_err_t ChipTemperature::init() noexcept {
    temperature_sensor_config_t cfg =
        TEMPERATURE_SENSOR_CONFIG_DEFAULT(kRangeMinC, kRangeMaxC);

    temperature_sensor_handle_t handle = nullptr;
    esp_err_t err = temperature_sensor_install(&cfg, &handle);
    if (err != ESP_OK) {
        return err;
    }
    sensor_ = handle;

    err = temperature_sensor_enable(handle);
    if (err != ESP_OK) {
        temperature_sensor_uninstall(handle);
        sensor_ = nullptr;
        return err;
    }
    return ESP_OK;
}

std::int16_t ChipTemperature::read() noexcept {
    assert(sensor_ != nullptr &&
           "ChipTemperature::init() must be called before read()");

    float celsius = 0.0f;
    esp_err_t err = temperature_sensor_get_celsius(
        static_cast<temperature_sensor_handle_t>(sensor_), &celsius);
    if (err != ESP_OK) {
        return 0;
    }
    return to_x100(celsius);
}

}  // namespace thermo::hal
