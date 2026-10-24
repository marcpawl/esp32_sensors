// Hardware abstraction: ESP32 on-die chip temperature sensor (§2.1, §10), real
// implementation for the development board.
//
// The shared vocabulary (`ChipTemperatureLike`) lives in the header-only
// `hal_common` component (`hal/chip_temperature_types.hpp`); this header adds
// the concrete `ChipTemperature` class backed by the ESP-IDF internal
// temperature sensor driver (`esp_driver_tsens`). `hal_stub` provides an
// identical `ChipTemperature` (same no-argument inline `create()` factory,
// `init()`, `read()`, plus a test-only `set_celsius()`) so a project can link
// either one and include `<hal/chip_temperature.hpp>` unchanged.
#pragma once

// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#include "esp_err.h"
#pragma GCC diagnostic pop
// NOLINTEND

#include <cstdint>

#include "hal/chip_temperature_types.hpp"

namespace thermo::hal {

// Reads the ESP32's internal temperature sensor (the on-die sensor, not the
// DS18B20s on the 1-Wire bus). This reports the die temperature, which runs
// warmer than ambient and is chiefly useful for calibration/telemetry.
class ChipTemperature {
  public:
    ChipTemperature() noexcept;

    ChipTemperature(ChipTemperature const&) = delete;
    // Movable so `create()` (and containers/factories) can hand a
    // ChipTemperature back by value without needing guaranteed copy elision.
    ChipTemperature(ChipTemperature&&) noexcept = default;
    ~ChipTemperature() noexcept;
    ChipTemperature& operator=(ChipTemperature const&) = delete;
    ChipTemperature& operator=(ChipTemperature&&) = delete;

    // Builds a chip-temperature sensor. Takes no parameters: the internal
    // sensor has no pin. Same signature for the hardware and stub
    // implementations; call init() before read().
    [[nodiscard]] static ChipTemperature create() noexcept {
        return ChipTemperature();
    }

    // Installs and enables the internal temperature sensor. Must be called
    // before read().
    esp_err_t init() noexcept;

    // Returns the die temperature in hundredths of a degree Celsius (e.g. 2350
    // == 23.50 °C), matching the DS18B20 reading units.
    [[nodiscard]] std::int16_t read() noexcept;

  private:
    // Opaque handle for the installed ESP-IDF temperature sensor; null until
    // init() succeeds. Kept as `void*` so this header does not pull in the
    // driver's C headers (they are included only in the .cpp).
    void* sensor_ = nullptr;
};

static_assert(ChipTemperatureLike<ChipTemperature>,
              "ChipTemperature must satisfy the ChipTemperatureLike concept");

}  // namespace thermo::hal
