// Hardware abstraction: DS18B20 1-Wire bus (§2.1, §2.4), deterministic stub for
// the host tests.
//
// The shared bus vocabulary (`OneWireLike`) lives in the header-only
// `hal_common` component (`hal/onewire_types.hpp`); this header adds the
// software-driven `OneWire` class. `hal_board` provides an identical `OneWire`
// (same ctor, inline `create()` factory, `init()`, `discover_sensors()`,
// `read()`) so a project can link either one and include `<hal/onewire.hpp>`
// unchanged.
//
// This stub replaces the GPIO-backed `hal_board` implementation where no real
// bus exists. The sensor collection is held in software and can be changed with
// `set_sensors()`, so the host tests control exactly which ROM IDs
// `discover_sensors()` reports and which readings `read()` returns.
#pragma once

// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#include "esp_err.h"
#pragma GCC diagnostic pop
// NOLINTEND

#include <vector>

#include "hal/onewire_types.hpp"

namespace thermo::hal {

// Represents a 1-Wire bus whose sensor collection is held in software.
class OneWire {
  public:
    // `gpio` is the 1-Wire data pin. The stub ignores it.
    explicit OneWire(int gpio) noexcept;

    OneWire(OneWire const&) = delete;
    // Movable so `create()` (and containers/factories) can hand a OneWire back
    // by value without needing guaranteed copy elision.
    OneWire(OneWire&&) noexcept = default;
    ~OneWire() noexcept = default;
    OneWire& operator=(OneWire const&) = delete;
    OneWire& operator=(OneWire&&) = delete;

    // Builds a stub bus for the given pin. Same signature for the hardware and
    // stub implementations; call init() before discover_sensors()/read().
    [[nodiscard]] static OneWire create(int gpio) noexcept {
        return OneWire(gpio);
    }

    // No-op for the stub (there is no pin to configure); always succeeds.
    esp_err_t init() noexcept;

    // Returns the ROM IDs of the sensors previously set with `set_sensors()`,
    // in order (extracted from the readings).
    [[nodiscard]] std::vector<RomId> discover_sensors() const noexcept;

    // Returns the readings previously set with `set_sensors()`, in order.
    [[nodiscard]] std::vector<DS18B20Reading> read() const noexcept;

    // Sets the sensor readings reported by subsequent `read()` and
    // `discover_sensors()` calls; the ROM IDs are extracted from the readings.
    // Intended for host tests.
    void set_sensors(std::vector<DS18B20Reading> readings) noexcept;

  private:
    std::vector<DS18B20Reading> sensors_;
};

static_assert(OneWireLike<OneWire>,
              "OneWire must satisfy the OneWireLike concept");

}  // namespace thermo::hal
