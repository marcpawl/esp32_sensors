// Run-mode entry point (§3.3 - §3.10).
//
// NOTE: Run mode is intentionally stubbed for this milestone. The structure
// mirrors the spec's sampling loop so it can be filled in incrementally.
#pragma once

#include "led_task.hpp"

namespace thermo {

// Executes exactly one Run-mode wake cycle: sample, buffer, (maybe) upload,
// then deep sleep. Because deep sleep reboots the MCU, this function does not
// normally return.
// `led_task` drives the LED indication for the cycle.
[[noreturn]] void run_mode_cycle(LedTask& led_task);

}  // namespace thermo
