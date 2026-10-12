// Configure-mode entry point (§3.2, §5, §6).
#pragma once

#include "esp_err.h"
#include "led_task.hpp"

namespace thermo {

// Runs the Configure-mode workflow:
//   - both LEDs solid (§7)
//   - Wi-Fi AP starts (§3.2)
//   - HTTPS server serves the config UI on port 443 (§6)
//
// `led_task` drives the LED indication for the duration of Configure mode.
//
// The ESP32 will reboot when the switch is changed, otherwise we
// stay in Configure mode.
[[noreturn]]
void run_configure_mode(LedTask& led_task);

}  // namespace thermo
