// Shared, implementation-free types for the DS18B20 1-Wire thermometers
// (§2.1, §5.1.3, §6.1).
//
// This header is the *single* definition of the DS18B20 vocabulary — the
// `RomId` device identity and a single `DS18B20Reading` — used by the sensor
// layer, the configuration UI, and the sample buffer. It carries no hardware
// or ESP-IDF dependency, so it can be shared by the firmware and by host
// tests.
//
// `RomId`'s textual helpers (`to_hex` / `from_hex`) are declared here but
// defined in `main/rom_id.cpp`, which stays free of ESP-IDF dependencies so
// the hex round-trip and validation rules can be unit-tested on the host.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace thermo::hal {

// A DS18B20 ROM ID is always 8 bytes (family code + serial + CRC).
inline constexpr std::size_t kRomIdBytes = 8;

// 8-byte DS18B20 ROM ID.
struct RomId {
    std::array<std::uint8_t, kRomIdBytes> bytes{};

    bool operator==(const RomId& other) const { return bytes == other.bytes; }

    // Uppercase hex, 16 chars (matches §5.1.3 / §6.1 example formatting).
    std::string to_hex() const;

    // Parses 16 hex chars (case-insensitive). Returns nullopt on bad input.
    static std::optional<RomId> from_hex(std::string_view hex);
};

// A single reading for one sensor.
struct DS18B20Reading {
    RomId rom{};
    std::optional<std::int16_t> temp_c_x100;  // nullopt == missing/null.
};

}  // namespace thermo::hal
