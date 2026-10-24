// Real, bit-banged DS18B20 1-Wire bus implementation for the dev board
// (§2.1, §2.4).
//
// ESP-IDF v6.1 ships no DS18B20/1-Wire driver, so the protocol is implemented
// directly on a single GPIO. Timing-sensitive sections run inside a critical
// section (interrupts disabled) to keep the bit slots within the 1-Wire spec.
//
// `discover_sensors()` runs the standard 1-Wire search (binary tree walk) so
// *every* DS18B20 on the shared bus is found, not just one.
// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#pragma GCC diagnostic pop
// NOLINTEND

#include "hal/onewire.hpp"

namespace thermo::hal {
namespace {

constexpr std::uint8_t kRomCommandSearchRom = 0xF0;
constexpr std::uint8_t kRomCommandSkipRom = 0xCC;
constexpr std::uint8_t kRomCommandMatchRom = 0x55;
constexpr std::uint8_t kRomCommandConvertT = 0x44;
constexpr std::uint8_t kRomCommandReadScratchpad = 0xBE;
constexpr std::uint8_t kFamilyDs18b20 = 0x28;

// DS18B20 12-bit conversion takes ~750 ms; wait a little longer.
constexpr std::uint32_t kConversionWaitUs = 800'000;

// Microsecond spin using the ROM delay (accurate enough for 1-Wire slots).
inline void delay_us(std::uint32_t us) noexcept {
    esp_rom_delay_us(us);
}

// A critical-section guard that disables interrupts for tight bit timing.
class IrqGuard {
  public:
    IrqGuard() noexcept { portENTER_CRITICAL(&mux_); }

    ~IrqGuard() noexcept { portEXIT_CRITICAL(&mux_); }

    IrqGuard(IrqGuard const&) = delete;
    IrqGuard& operator=(IrqGuard const&) = delete;

  private:
    portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
};

inline void drive_low(int gpio) noexcept {
    gpio_set_level(static_cast<gpio_num_t>(gpio), 0);
}

inline void release(int gpio) noexcept {
    gpio_set_level(static_cast<gpio_num_t>(gpio), 1);
}

inline int sample(int gpio) noexcept {
    return gpio_get_level(static_cast<gpio_num_t>(gpio));
}

// Dallas/Maxim CRC8, reflected polynomial 0x8C.
std::uint8_t crc8(const std::uint8_t* data, std::size_t len) noexcept {
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

}  // namespace

OneWire::OneWire(int gpio) noexcept : gpio_(gpio) {}

OneWire::~OneWire() noexcept {
    if (initialized_) {
        gpio_reset_pin(static_cast<gpio_num_t>(gpio_));
    }
}

esp_err_t OneWire::init() noexcept {
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = 1ULL << gpio_;
    cfg.mode = GPIO_MODE_INPUT_OUTPUT_OD;  // Open-drain.
    cfg.pull_up_en = GPIO_PULLUP_ENABLE;   // Internal pull-up on top of 4.7k.
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.intr_type = GPIO_INTR_DISABLE;
    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        return err;
    }
    release(gpio_);
    initialized_ = true;
    return ESP_OK;
}

bool OneWire::reset() noexcept {
    IrqGuard guard;
    drive_low(gpio_);
    delay_us(480);
    release(gpio_);
    delay_us(70);
    const bool presence = (sample(gpio_) == 0);
    delay_us(410);
    return presence;
}

void OneWire::write_bit(bool bit) noexcept {
    IrqGuard guard;
    if (bit) {
        drive_low(gpio_);
        delay_us(6);
        release(gpio_);
        delay_us(64);
    } else {
        drive_low(gpio_);
        delay_us(60);
        release(gpio_);
        delay_us(10);
    }
}

bool OneWire::read_bit() noexcept {
    IrqGuard guard;
    drive_low(gpio_);
    delay_us(6);
    release(gpio_);
    delay_us(9);
    const bool value = (sample(gpio_) != 0);
    delay_us(55);
    return value;
}

void OneWire::write_byte(std::uint8_t value) noexcept {
    for (int i = 0; i < 8; ++i) {
        write_bit((value >> i) & 0x01);
    }
}

std::uint8_t OneWire::read_byte() noexcept {
    std::uint8_t value = 0;
    for (int i = 0; i < 8; ++i) {
        if (read_bit()) {
            value |= static_cast<std::uint8_t>(1u << i);
        }
    }
    return value;
}

std::vector<RomId> OneWire::discover_sensors() noexcept {
    // Standard 1-Wire search (Maxim AN187): a binary tree walk over the 64-bit
    // ROM space. `last_discrepancy` remembers where the previous pass chose the
    // '0' branch, so each pass enumerates one device until none remain.
    std::vector<RomId> found;
    if (!reset()) {
        return found;
    }

    std::uint8_t rom[kRomIdBytes] = {};
    int last_discrepancy = 0;
    bool last_device = false;

    while (!last_device) {
        if (!reset()) {
            break;
        }
        write_byte(kRomCommandSearchRom);

        int discrepancy = 0;
        for (int bit = 0; bit < 64; ++bit) {
            const int id_bit = read_bit() ? 1 : 0;
            const int cmp_bit = read_bit() ? 1 : 0;

            if (id_bit == 1 && cmp_bit == 1) {
                // No device drives the bus: search is over / invalid.
                return found;
            }

            int direction = 0;
            if (id_bit != cmp_bit) {
                // Bits differ: every device has a deterministic value here.
                direction = id_bit;
            } else {
                // Both 0 or both 1: a branch point. Go the same way as last
                // time unless this is the discrepancy we are flipping.
                if (bit == last_discrepancy) {
                    direction = 1;
                } else if (bit > last_discrepancy) {
                    direction = 0;
                    discrepancy = bit;
                } else {
                    direction = (rom[bit / 8] >> (bit % 8)) & 0x01;
                }
            }

            if (direction) {
                rom[bit / 8] =
                    static_cast<std::uint8_t>(rom[bit / 8] | (1u << (bit % 8)));
            } else {
                rom[bit / 8] = static_cast<std::uint8_t>(rom[bit / 8] &
                                                         ~(1u << (bit % 8)));
            }
            write_bit(direction != 0);
        }

        last_discrepancy = discrepancy;
        if (last_discrepancy == 0) {
            last_device = true;
        }

        // Accept only genuine DS18B20s (family 0x28) with a valid CRC.
        if (rom[0] == kFamilyDs18b20 && crc8(rom, 7) == rom[7]) {
            RomId id{};
            for (std::size_t i = 0; i < kRomIdBytes; ++i) {
                id.bytes[i] = rom[i];
            }
            found.push_back(id);
        }
    }

    return found;
}

std::optional<std::int16_t> OneWire::read_temperature(
    const RomId& rom) noexcept {
    if (!reset()) {
        return std::nullopt;
    }
    write_byte(kRomCommandMatchRom);
    for (std::uint8_t b : rom.bytes) {
        write_byte(b);
    }
    write_byte(kRomCommandReadScratchpad);

    std::uint8_t scratch[9] = {};
    for (std::size_t i = 0; i < sizeof(scratch); ++i) {
        scratch[i] = read_byte();
    }
    if (crc8(scratch, 8) != scratch[8]) {
        return std::nullopt;
    }

    const std::int16_t raw =
        static_cast<std::int16_t>((scratch[1] << 8) | scratch[0]);
    // Default 12-bit resolution: 0.0625 degC/LSB => raw/16 == degC x100.
    return static_cast<std::int16_t>(raw * 100 / 16);
}

std::vector<DS18B20Reading> OneWire::read() noexcept {
    const std::vector<RomId> roms = discover_sensors();

    std::vector<DS18B20Reading> readings;
    readings.reserve(roms.size());
    if (roms.empty()) {
        return readings;
    }

    // One conversion for all devices (SKIP ROM + CONVERT T), then wait once.
    if (reset()) {
        write_byte(kRomCommandSkipRom);
        write_byte(kRomCommandConvertT);
        delay_us(kConversionWaitUs);
    }

    for (const RomId& rom : roms) {
        readings.push_back({rom, read_temperature(rom)});
    }
    return readings;
}

}  // namespace thermo::hal
