// Run-mode entry point (§3.3 - §3.10).
//
// NOTE: Run mode is intentionally stubbed for this milestone. The structure
// mirrors the spec's sampling loop so it can be filled in incrementally.
#pragma once

// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#pragma GCC diagnostic pop
// NOLINTEND

#include "hal/board.hpp"
#include "hal/board_types.hpp"
#include "logic/led_task.hpp"

namespace thermo {

// Executes exactly one Run-mode wake cycle: sample, buffer, (maybe) upload,
// then deep sleep. Because deep sleep reboots the MCU, this function does not
// normally return.
//
// `board` is the board to sample and to sleep; `led_task` drives the LED
// indication for the cycle. Both must already be initialized, and `led_task`
// must be running on the same board (it shares it, so the two may be used
// concurrently). `Board` is any `hal::BoardLike` type.
template<hal::BoardLike Board>
[[noreturn]] void run_mode_cycle(Board& board, LedTask<Board>& led_task);

// Entry point used by `app_main`: runs one cycle on the board the firmware
// links in (`hal_board` on target, `hal_stub` in the host tests). Defined
// in run_mode.cpp.
[[noreturn]] void run_mode_cycle(hal::Board& board,
                                 LedTask<hal::Board>& led_task);

}  // namespace thermo
