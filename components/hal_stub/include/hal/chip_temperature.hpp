// Hardware abstraction: ESP32 on-die chip temperature sensor (§2.1, §10),
// deterministic stub for the host tests.
//
// The shared vocabulary (`ChipTemperatureLike`) lives in the header-only
// `hal_common` component (`hal/chip_temperature_types.hpp`); this header adds
// the software-driven `ChipTemperature` class. `hal_board` provides an
// identical `ChipTemperature` (same no-argument inline `create()` factory,
// `init()`, `read()`), so a project can link either one and include
// `<hal/chip_temperature.hpp>` unchanged.
//
// This stub replaces the driver-backed `hal_board` implementation where no
// internal sensor is available (or should be touched). The reported
// temperature is held in software and can be changed with `set_celsius()`, so
// host tests control exactly what `read()` returns.
#pragma once

#include <cstdint>

#include "esp_err.h"
#include "hal/chip_temperature_types.hpp"

namespace thermo::hal {

// Represents the chip temperature sensor with its reading held in software.
class ChipTemperature {
  public:
    ChipTemperature() noexcept;

    ChipTemperature(ChipTemperature const&) = delete;
    // Movable so `create()` (and containers/factories) can hand a
    // ChipTemperature back by value without needing guaranteed copy elision.
    ChipTemperature(ChipTemperature&&) noexcept = default;
    ~ChipTemperature() noexcept = default;
    ChipTemperature& operator=(ChipTemperature const&) = delete;
    ChipTemperature& operator=(ChipTemperature&&) = delete;

    // Builds a stub sensor. Takes no parameters: the internal sensor has no
    // pin. Same signature for the hardware and stub implementations; call
    // init() before read().
    [[nodiscard]] static ChipTemperature create() noexcept {
        return ChipTemperature();
    }

    // No-op for the stub (there is nothing to configure); always succeeds.
    esp_err_t init() noexcept;

    // Returns the temperature previously set with `set_celsius()`, in
    // hundredths of a degree Celsius (e.g. 2350 == 23.50 °C).
    [[nodiscard]] std::int16_t read() const noexcept;

    // Sets the temperature reported by subsequent `read()` calls, in
    // hundredths of a degree Celsius. Intended for host tests.
    void set_celsius(std::int16_t celsius_x100) noexcept;

  private:
    // Default die temperature (x100 °C); a plausible room-temperature value so
    // callers see realistic data without configuring anything.
    static constexpr std::int16_t kDefaultCelsiusX100 = 2500;
    std::int16_t celsius_x100_ = kDefaultCelsiusX100;
};

static_assert(ChipTemperatureLike<ChipTemperature>,
              "ChipTemperature must satisfy the ChipTemperatureLike concept");

}  // namespace thermo::hal
