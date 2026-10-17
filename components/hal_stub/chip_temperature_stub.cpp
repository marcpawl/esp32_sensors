// Deterministic chip-temperature stub for QEMU and host tests.
//
// There is no internal sensor to read, so the temperature is held in software.
// `init()` always succeeds; `read()` returns whatever `set_celsius()` last set
// (defaulting to a plausible room temperature).
#include "hal/chip_temperature.hpp"

namespace thermo::hal {

ChipTemperature::ChipTemperature() noexcept = default;

esp_err_t ChipTemperature::init() noexcept {
    return ESP_OK;
}

std::int16_t ChipTemperature::read() const noexcept {
    return celsius_x100_;
}

void ChipTemperature::set_celsius(std::int16_t celsius_x100) noexcept {
    celsius_x100_ = celsius_x100;
}

}  // namespace thermo::hal
