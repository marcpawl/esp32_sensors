// Hardware abstraction: DS18B20 1-Wire bus (§2.1, §2.4), real, GPIO-backed
// implementation for the development board.
//
// The shared bus vocabulary (`OneWireLike`) lives in the header-only
// `hal_common` component (`hal/onewire_types.hpp`); this header adds the
// concrete, bit-banged `OneWire` class. `hal_stub` provides an identical
// `OneWire` (same ctor, inline `create()` factory, `init()`,
// `discover_sensors()`, `read()`, plus a test-only `set_sensors()`) so a
// project can link either one and include `<hal/onewire.hpp>` unchanged.
//
// The bus exposes the ROM IDs present on the wire and a reading (temperature)
// for each of them.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "esp_err.h"
#include "hal/onewire_types.hpp"

namespace thermo::hal {

// Bit-banged 1-Wire bus on a single GPIO. ESP-IDF v6.1 ships no DS18B20 driver,
// so the protocol is implemented directly. Not thread-safe: callers must
// serialize access.
class OneWire {
  public:
    // `gpio` is the 1-Wire data pin.
    explicit OneWire(int gpio) noexcept;

    OneWire(OneWire const&) = delete;
    // Movable so `create()` (and containers/factories) can hand a OneWire back
    // by value without needing guaranteed copy elision.
    OneWire(OneWire&&) noexcept = default;
    ~OneWire() noexcept;
    OneWire& operator=(OneWire const&) = delete;
    OneWire& operator=(OneWire&&) = delete;

    // Builds a bus for the given data pin. Same signature for the hardware and
    // stub implementations; call init() before discover_sensors().
    [[nodiscard]] static OneWire create(int gpio) noexcept {
        return OneWire(gpio);
    }

    // Configures the GPIO as open-drain and releases the bus. Must be called
    // before discover_sensors().
    esp_err_t init() noexcept;

    // Walks the 1-Wire search tree and returns the ROM IDs of every DS18B20
    // (family code 0x28) present on the bus. Returns an empty vector when no
    // device responds.
    [[nodiscard]] std::vector<RomId> discover_sensors() noexcept;

    // Discovers the sensors on the bus and reads a temperature from each,
    // returning one reading (ROM ID + temperature) per sensor, in discovery
    // order. A sensor whose scratchpad read fails or whose CRC is bad yields a
    // reading with `temp_c_x100 == std::nullopt`.
    [[nodiscard]] std::vector<DS18B20Reading> read() noexcept;

  private:
    // Low-level bit-slot primitives (1-Wire protocol).
    bool reset() noexcept;
    void write_bit(bool bit) noexcept;
    bool read_bit() noexcept;
    void write_byte(std::uint8_t value) noexcept;
    std::uint8_t read_byte() noexcept;

    // Starts a conversion on every device, then reads one sensor's scratchpad.
    // Returns nullopt if the device does not respond or its CRC fails.
    std::optional<std::int16_t> read_temperature(const RomId& rom) noexcept;

    int const gpio_;
    bool initialized_ = false;
};

static_assert(OneWireLike<OneWire>,
              "OneWire must satisfy the OneWireLike concept");

}  // namespace thermo::hal
