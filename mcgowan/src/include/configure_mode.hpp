// Configure-mode entry point (§3.2, §5, §6).
#pragma once

#include "esp_err.h"
#include "hal/board.hpp"
#include "hal/board_types.hpp"
#include "led_task.hpp"

namespace thermo {

// Runs the Configure-mode workflow:
//   - both LEDs solid (§7)
//   - Wi-Fi AP starts (§3.2)
//   - HTTPS server serves the config UI on port 443 (§6)
//
// `led_task` drives the LED indication for the duration of Configure mode.
// `Board` is the board type the LED task runs on (`hal::BoardLike`).
//
// The ESP32 will reboot when the switch is changed, otherwise we
// stay in Configure mode.
template<hal::BoardLike Board>
[[noreturn]] void run_configure_mode(LedTask<Board>& led_task);

// Entry point used by `app_main`: runs the Configure-mode workflow on the board
// the firmware links in (`hal_board` on target, `hal_stub` under QEMU/test
// builds). Defined in configure_mode.cpp.
[[noreturn]] void run_configure_mode(LedTask<hal::Board>& led_task);

}  // namespace thermo
