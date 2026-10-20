// Run-mode entry point (§3.3 - §3.10).
//
// NOTE: Run mode is intentionally stubbed for this milestone. The structure
// mirrors the spec's sampling loop so it can be filled in incrementally.
#pragma once

#include "hal/board.hpp"
#include "hal/board_types.hpp"
#include "logic/led_task.hpp"

namespace thermo {

// Executes exactly one Run-mode wake cycle: sample, buffer, (maybe) upload,
// then deep sleep. Because deep sleep reboots the MCU, this function does not
// normally return.
// `led_task` drives the LED indication for the cycle. `Board` is the board
// type the LED task runs on (`hal::BoardLike`).
template<hal::BoardLike Board>
[[noreturn]] void run_mode_cycle(LedTask<Board>& led_task);

// Entry point used by `app_main`: runs one cycle on the board the firmware
// links in (`hal_board` on target, `hal_stub` under QEMU/test builds). Defined
// in run_mode.cpp.
[[noreturn]] void run_mode_cycle(LedTask<hal::Board>& led_task);

}  // namespace thermo
