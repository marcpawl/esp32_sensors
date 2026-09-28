// DS18B20 device operations on the shared 1-Wire bus.
#include "sensor.hpp"

#include <cstring>

namespace thermo {
namespace {

constexpr std::uint8_t kRomCommandSkipRom = 0xCC;
constexpr std::uint8_t kRomCommandMatchRom = 0x55;
constexpr std::uint8_t kRomCommandReadRom = 0x33;
constexpr std::uint8_t kRomCommandConvertT = 0x44;
constexpr std::uint8_t kRomCommandReadScratchpad = 0xBE;

constexpr std::uint8_t kFamilyDs18b20 = 0x28;

std::uint8_t crc8(const std::uint8_t* data, std::size_t len) {
    std::uint8_t crc = 0;
    for (std::size_t i = 0; i < len; ++i) {
        std::uint8_t in = data[i];
        for (int b = 0; b < 8; ++b) {
            const std::uint8_t mix = (crc ^ in) & 0x01;
            crc >>= 1;
            if (mix) {
                crc ^= 0x8C;
            }
            in >>= 1;
        }
    }
    return crc;
}

} // namespace

std::vector<RomId> Ds18b20::scan() {
    // Full ROM search (binary tree walk) is not implemented; this simple form
    // works only when a single device is present. Run mode avoids scanning
    // entirely by using cached ROM IDs (§3.4).
    std::vector<RomId> found;
    if (!bus_.reset()) {
        return found;
    }
    bus_.write_byte(kRomCommandReadRom);
    RomId id{};
    for (std::size_t i = 0; i < id.bytes.size(); ++i) {
        id.bytes[i] = bus_.read_byte();
    }
    if (id.bytes[0] == kFamilyDs18b20 && crc8(id.bytes.data(), 7) == id.bytes[7]) {
        found.push_back(id);
    }
    return found;
}

esp_err_t Ds18b20::start_conversion() {
    if (!bus_.reset()) {
        return ESP_ERR_NOT_FOUND;
    }
    bus_.write_byte(kRomCommandSkipRom);
    bus_.write_byte(kRomCommandConvertT);
    return ESP_OK;
}

std::optional<std::int16_t> Ds18b20::read_temperature(const RomId& rom) {
    if (!bus_.reset()) {
        return std::nullopt;
    }
    bus_.write_byte(kRomCommandMatchRom);
    for (std::uint8_t b : rom.bytes) {
        bus_.write_byte(b);
    }
    bus_.write_byte(kRomCommandReadScratchpad);

    std::uint8_t scratch[9];
    for (std::size_t i = 0; i < sizeof(scratch); ++i) {
        scratch[i] = bus_.read_byte();
    }
    if (crc8(scratch, 8) != scratch[8]) {
        return std::nullopt;
    }

    const std::int16_t raw =
        static_cast<std::int16_t>((scratch[1] << 8) | scratch[0]);
    // Default 12-bit resolution: 0.0625 degC/LSB => raw/16 == degC x100.
    return static_cast<std::int16_t>(raw * 100 / 16);
}

} // namespace thermo
