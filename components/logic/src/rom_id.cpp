// DS18B20 ROM-ID formatting and parsing (§5.1.3, §6.1).
//
// Part of the `logic` component so the firmware and the host tests share one
// implementation; free of ESP-IDF dependencies, so it builds for the `linux`
// target. The ROM ID *type* is the HAL's data (`thermo::hal::RomId`, from
// `hal_common`'s `hal/ds18b20_types.hpp`); the textual conversions are
// application behaviour and are plain functions here rather than members.
// The 1-Wire transport itself lives in the firmware's `onewire_bus.cpp`.
#include "logic/rom_id.hpp"

// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#pragma GCC diagnostic pop
// NOLINTEND

#include <cstdio>

namespace thermo {

std::string rom_id_to_hex(const hal::RomId& rom) {
    // 8 bytes -> 16 uppercase hex chars, no separator.
    char buf[2 * hal::kRomIdBytes + 1];
    for (std::size_t i = 0; i < rom.bytes.size(); ++i) {
        std::snprintf(buf + i * 2, 3, "%02X", rom.bytes[i]);
    }
    buf[2 * hal::kRomIdBytes] = '\0';
    return buf;
}

std::optional<hal::RomId> rom_id_from_hex(std::string_view hex) {
    hal::RomId id{};
    // ROM IDs are always exactly 16 hex characters.
    if (hex.size() != 2 * hal::kRomIdBytes) {
        return std::nullopt;
    }
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') {
            return c - '0';
        }
        if (c >= 'a' && c <= 'f') {
            return c - 'a' + 10;
        }
        if (c >= 'A' && c <= 'F') {
            return c - 'A' + 10;
        }
        return -1;
    };
    for (std::size_t i = 0; i < id.bytes.size(); ++i) {
        const int hi = nibble(hex[i * 2]);
        const int lo = nibble(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) {
            return std::nullopt;
        }
        id.bytes[i] = static_cast<std::uint8_t>((hi << 4) | lo);
    }
    return id;
}

}  // namespace thermo
