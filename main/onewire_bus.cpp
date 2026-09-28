// Bit-banged 1-Wire bus for DS18B20 sensors.
//
// ESP-IDF v6.1 does not ship a DS18B20/1-Wire driver, so the protocol is
// implemented directly on a single GPIO. Timing-sensitive sections run inside
// a critical section (interrupts disabled) to keep the bit slots within the
// 1-Wire spec. The bus is not thread-safe; callers must serialize access.
#include "sensor.hpp"

#include <cstring>

#include "esp_log.h"
#include "esp_rom_sys.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace thermo {
namespace {

// Microsecond spin using the ROM delay (accurate enough for 1-Wire slots).
inline void delay_us(std::uint32_t us) { esp_rom_delay_us(us); }

// A critical-section guard that disables interrupts for tight bit timing.
// Uses `portDISABLE_INTERRUPTS`/`portENABLE_INTERRUPTS` via the task-level
// critical section API, which is safe to nest inside FreeRTOS tasks.
class IrqGuard {
  public:
    IrqGuard() { portENTER_CRITICAL(&mux_); }
    ~IrqGuard() { portEXIT_CRITICAL(&mux_); }

    IrqGuard(const IrqGuard&) = delete;
    IrqGuard& operator=(const IrqGuard&) = delete;

  private:
    portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
};

// Drive the line low (output 0).
inline void drive_low(int gpio) {
    gpio_set_level(static_cast<gpio_num_t>(gpio), 0);
}

// Release the line and let the pull-up take it high.
inline void release(int gpio) {
    gpio_set_level(static_cast<gpio_num_t>(gpio), 1);
}

inline int sample(int gpio) { return gpio_get_level(static_cast<gpio_num_t>(gpio)); }

} // namespace

OneWireBus::OneWireBus(int gpio) : gpio_(gpio) {}

OneWireBus::~OneWireBus() {
    if (initialized_) {
        gpio_reset_pin(static_cast<gpio_num_t>(gpio_));
    }
}

esp_err_t OneWireBus::init() {
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = 1ULL << gpio_;
    cfg.mode = GPIO_MODE_INPUT_OUTPUT_OD; // Open-drain.
    cfg.pull_up_en = GPIO_PULLUP_ENABLE;  // Internal pull-up on top of 4.7k.
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

bool OneWireBus::reset() {
    bool presence = false;
    IrqGuard guard;
    drive_low(gpio_);
    delay_us(480);
    release(gpio_);
    delay_us(70);
    presence = (sample(gpio_) == 0);
    delay_us(410);
    return presence;
}

void OneWireBus::write_bit(bool bit) {
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

bool OneWireBus::read_bit() {
    IrqGuard guard;
    drive_low(gpio_);
    delay_us(6);
    release(gpio_);
    delay_us(9);
    const bool value = (sample(gpio_) != 0);
    delay_us(55);
    return value;
}

void OneWireBus::write_byte(std::uint8_t value) {
    for (int i = 0; i < 8; ++i) {
        write_bit((value >> i) & 0x01);
    }
}

std::uint8_t OneWireBus::read_byte() {
    std::uint8_t value = 0;
    for (int i = 0; i < 8; ++i) {
        if (read_bit()) {
            value |= (1u << i);
        }
    }
    return value;
}

std::uint8_t OneWireBus::crc8(const std::uint8_t* data, std::size_t len) {
    // Dallas/Maxim CRC8, reflected polynomial 0x8C.
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

bool OneWireBus::crc8_ok(const std::uint8_t* data, std::size_t len) {
    return crc8(data, len) == data[len];
}

std::string RomId::to_hex() const {
    char buf[17];
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        std::snprintf(buf + i * 2, 3, "%02X", bytes[i]);
    }
    buf[16] = '\0';
    return buf;
}

std::optional<RomId> RomId::from_hex(std::string_view hex) {
    RomId id{};
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
    for (std::size_t i = 0; i < 8; ++i) {
        const int hi = nibble(hex[i * 2]);
        const int lo = nibble(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) {
            return std::nullopt;
        }
        id.bytes[i] = static_cast<std::uint8_t>((hi << 4) | lo);
    }
    return id;
}

} // namespace thermo
