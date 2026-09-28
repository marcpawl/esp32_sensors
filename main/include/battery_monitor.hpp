// Battery voltage measurement via ADC on GPIO 34 (§2.5) and the
// high-water / low-water hysteresis state machine (§3.6).
#pragma once

#include <cstdint>
#include <optional>

#include "app_config.hpp"
#include "esp_err.h"
#include "led_manager.hpp" // TxState

namespace thermo {

class BatteryMonitor {
  public:
    BatteryMonitor() = default;

    // Configures the ADC (12 dB attenuation) and curve-fitting calibration.
    esp_err_t init();

    // Reads battery voltage in millivolts, applying the divider ratio and the
    // configured calibration gain/offset. Returns nullopt on read failure.
    // When divider gating is enabled (§2.6), the gate is energized around the
    // read only.
    std::optional<std::uint32_t> read_mv(const Config& cfg);

    // Applies the §3.6 hysteresis rules against `current` state.
    // Pure w.r.t. hardware; exposed as a static for host testing.
    static TxState evaluate_tx_state(std::uint32_t batt_mv, const Config& cfg,
                                     TxState current);

  private:
    bool initialized_ = false;
    int gate_gpio_ = -1;
};

} // namespace thermo
