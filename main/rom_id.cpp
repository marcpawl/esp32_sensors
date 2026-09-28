// DS18B20 ROM-ID formatting and parsing (§5.1.3, §6.1).
//
// Kept free of ESP-IDF dependencies so the hex round-trip and validation rules
// can be unit-tested on the host target — the same rationale as temp_format.cpp
// and payload.cpp. The 1-Wire transport itself lives in onewire_bus.cpp.
#include "sensor.hpp"

#include <cstdio>

namespace thermo {

std::string RomId::to_hex() const {
    // 8 bytes -> 16 uppercase hex chars, no separator.
    char buf[18];
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        std::snprintf(buf + i * 2, 3, "%02X", bytes[i]);
    }
    buf[16] = '\0';
    return buf;
}

std::optional<RomId> RomId::from_hex(std::string_view hex) {
    RomId id{};
    // ROM IDs are always exactly 16 hex characters.
    if (hex.size() != 16) {
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
