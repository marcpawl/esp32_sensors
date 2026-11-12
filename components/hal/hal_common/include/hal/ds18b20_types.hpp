// Shared, implementation-free types for the DS18B20 1-Wire thermometers
// (§2.1, §5.1.3, §6.1).
//
// This header is the *single* definition of the DS18B20 vocabulary — the
// `RomId` device identity and a single `DS18B20Reading` — used by the sensor
// layer, the configuration UI, and the sample buffer. It carries no hardware
// or ESP-IDF dependency, so it can be shared by the firmware and by host
// tests.
//
// `RomId` is pure data: the struct holds the 8 raw bytes and compares by
// value, and nothing else. Its textual form is application behaviour, not
// hardware vocabulary, so the conversions are free functions in the `logic`
// component (`logic/rom_id.hpp`: `thermo::rom_id_to_hex()` /
// `thermo::rom_id_from_hex()`).
#pragma once

// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#pragma GCC diagnostic pop
// NOLINTEND

#include <array>
#include <optional>

#include <cstddef>
#include <cstdint>

namespace thermo::hal {

// A DS18B20 ROM ID is always 8 bytes (family code + serial + CRC).
inline constexpr std::size_t kRomIdBytes = 8;

// 8-byte DS18B20 ROM ID. The raw device identity; see `logic/rom_id.hpp` to
// render it as the §5.1.3 / §6.1 uppercase hex key or to parse one.
struct RomId {
    std::array<std::uint8_t, kRomIdBytes> bytes{};

    bool operator==(const RomId& other) const { return bytes == other.bytes; }
};

// A single reading for one sensor.
struct DS18B20Reading {
    RomId rom{};
    std::optional<std::int16_t> temp_c_x100;  // nullopt == missing/null.
};

}  // namespace thermo::hal
