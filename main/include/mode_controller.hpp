// Reads the 3-position ON-OFF-ON switch (§2.3, §3).
#pragma once

#include "mode.hpp"
#include "esp_err.h"

namespace thermo {

class ModeController {
  public:
    ModeController(int config_gpio, int run_gpio);

    esp_err_t init();

    // Samples the switch. Configure wins ties (both HIGH) to make it the
    // deliberate, higher-priority state.
    Mode read() const;

  private:
    int config_gpio_;
    int run_gpio_;
};

} // namespace thermo
