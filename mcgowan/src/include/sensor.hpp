// Sensor abstraction: 1-Wire bus + DS18B20 device access.
//
// The hardware-facing classes are kept behind small interfaces so that
// application logic (buffering, payload formatting) remains host-testable.
#pragma once

// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#include "esp_err.h"
#pragma GCC diagnostic pop
// NOLINTEND

#include <optional>
#include <vector>

#include <cstddef>
#include <cstdint>

#include "constants.hpp"
#include "hal/ds18b20_types.hpp"  // RomId, DS18B20Reading
#include "temp_format.hpp"        // kTempNull, format_temp_c_x100

namespace thermo {

// The DS18B20 identity and reading types live in the shared, implementation-
// free `hal_common` component (`hal/ds18b20_types.hpp`) so the sensor layer,
// the configuration UI, and host tests all agree on one definition. Re-export
// them into `thermo` for convenience.
using thermo::hal::DS18B20Reading;
using thermo::hal::RomId;

// Low-level 1-Wire bus operations. Bit-banged on a single GPIO since ESP-IDF
// v6.1 ships no DS18B20 driver.
class OneWireBus {
  public:
    explicit OneWireBus(int gpio);
    ~OneWireBus();

    OneWireBus(const OneWireBus&) = delete;
    OneWireBus& operator=(const OneWireBus&) = delete;

    // Initializes the GPIO and verifies at least one device responds.
    esp_err_t init();

    // Emits a reset pulse; returns true if a presence pulse was detected.
    bool reset();

    void write_byte(std::uint8_t value);
    std::uint8_t read_byte();
    void write_bit(bool bit);
    bool read_bit();

    // Dallas/Maxim CRC8 over `len` bytes.
    static std::uint8_t crc8(const std::uint8_t* data, std::size_t len);

    // True when `data[len]` is the correct CRC8 of the first `len` bytes.
    static bool crc8_ok(const std::uint8_t* data, std::size_t len);

  private:
    int gpio_;
    bool initialized_ = false;
};

// DS18B20 device operations on a shared bus.
class Ds18b20 {
  public:
    explicit Ds18b20(OneWireBus& bus) : bus_(bus) {}

    // Scans the bus for DS18B20 devices (family code 0x28). Used at first boot
    // and in Configure mode; Run mode uses cached ROM IDs instead (§3.4).
    std::vector<RomId> scan();

    // Starts a temperature conversion on all devices (SKIP ROM + CONVERT T).
    esp_err_t start_conversion();

    // Reads scratchpad temperature for a specific ROM ID. Returns nullopt if
    // the device does not respond or its CRC fails.
    std::optional<std::int16_t> read_temperature(const RomId& rom);

  private:
    OneWireBus& bus_;
};

// High-level sensor manager binding the bus, cached ROM IDs, and name mapping.
class SensorManager {
  public:
    explicit SensorManager(int gpio);

    esp_err_t init();

    // Reads all known sensors. When `roms` is empty, a bus scan is performed
    // and the discovered ROM IDs are returned via `out_roms`.
    std::vector<DS18B20Reading> read_all(const std::vector<RomId>& roms,
                                         std::vector<RomId>* out_roms);

  private:
    OneWireBus bus_;
    Ds18b20 device_;
};

}  // namespace thermo
