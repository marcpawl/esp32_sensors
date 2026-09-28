// Configure-mode entry point (§3.2, §5, §6).
#pragma once

#include "esp_err.h"

namespace thermo {

// Runs the Configure-mode workflow:
//   - both LEDs solid (§7)
//   - Wi-Fi AP starts (§3.2)
//   - HTTPS server serves the config UI on port 443 (§6)
//
// Blocks until the device is reconfigured away from Configure mode or reset.
void run_configure_mode();

}  // namespace thermo
