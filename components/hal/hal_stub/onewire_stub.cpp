// Deterministic 1-Wire bus stub for the host tests.
//
// There is no real bus, so the sensor collection is held in software.
// `init()` always succeeds; `read()` returns whatever `set_sensors()` last set
// and `discover_sensors()` returns the ROM IDs extracted from those readings.
// A default collection (the three example DS18B20 sensors from §5.1.3) is
// provided so callers see realistic data without configuring anything.
// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#pragma GCC diagnostic pop
// NOLINTEND

#include "hal_stub/onewire.hpp"

namespace thermo::hal::stub {
namespace {

// Example DS18B20 sensors from §5.1.3, used as the default collection. The
// temperatures are placeholders; tests that care set their own via
// `set_sensors()`.
inline constexpr DS18B20Reading kDefaultSensors[] = {
    {{{0x28, 0xFF, 0x64, 0xA1, 0xB2, 0x16, 0x03, 0xC4}}, std::nullopt},
    {{{0x28, 0xFF, 0x3B, 0x2C, 0x77, 0x16, 0x04, 0x2F}}, std::nullopt},
    {{{0x28, 0xFF, 0xAA, 0x19, 0xC3, 0x16, 0x04, 0xD1}}, std::nullopt},
};

}  // namespace

OneWire::OneWire(int gpio) noexcept {
    // Accepted for interface parity with the real implementation; unused.
    (void)gpio;
    for (const DS18B20Reading& reading : kDefaultSensors) {
        sensors_.push_back(reading);
    }
}

esp_err_t OneWire::init() noexcept {
    return ESP_OK;
}

RomIdList OneWire::discover_sensors() const noexcept {
    RomIdList roms;
    for (const DS18B20Reading& reading : sensors_) {
        roms.try_push_back(reading.rom);
    }
    return roms;
}

ReadingList OneWire::read() const noexcept {
    return sensors_;
}

void OneWire::set_sensors(ReadingList readings) noexcept {
    sensors_ = readings;
}

}  // namespace thermo::hal::stub
