// Sensor abstraction: 1-Wire bus + DS18B20 device access.
//
// The hardware-facing classes are kept behind small interfaces so that
// application logic (buffering, payload formatting) remains host-testable.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "constants.hpp"
#include "esp_err.h"
#include "temp_format.hpp"  // kTempNull, format_temp_c_x100

namespace thermo {

// 8-byte DS18B20 ROM ID.
struct RomId {
    std::array<std::uint8_t, limits::kRomIdBytes> bytes{};

    bool operator==(const RomId& other) const { return bytes == other.bytes; }

    // Uppercase hex, 16 chars (matches §5.1.3 / §6.1 example formatting).
    std::string to_hex() const;

    // Parses 16 hex chars (case-insensitive). Returns nullopt on bad input.
    static std::optional<RomId> from_hex(std::string_view hex);
};

// A single reading for one sensor.
struct SensorReading {
    RomId rom{};
    std::optional<std::int16_t> temp_c_x100;  // nullopt == missing/null.
};

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
    std::vector<SensorReading> read_all(const std::vector<RomId>& roms,
                                        std::vector<RomId>* out_roms);

  private:
    OneWireBus bus_;
    Ds18b20 device_;
};

}  // namespace thermo
