// The single `RTC_DATA_ATTR` state block and its per-boot initialization.
//
// This is the firmware-only half of §8: `logic/rtc_state.hpp` defines the
// block's *shape* and the buffer operations (and is host-testable), while this
// header owns the one thing that cannot be host-tested — placing the block in
// RTC slow memory with `RTC_DATA_ATTR` and asking the board why the last wake
// happened.
//
// The wake question goes through `hal::BoardLike::sleep_get_wakeup_causes()`
// rather than calling `esp_sleep_get_wakeup_causes()` directly, so the same
// code runs against the stub board (a test can simulate any wake) and the real
// GPIO board (which reads the ESP-IDF bitmap).
#pragma once

// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#pragma GCC diagnostic pop
// NOLINTEND

#include "hal/board_types.hpp"
#include "hal/sleep_types.hpp"
#include "logic/rtc_state.hpp"

namespace thermo {

// Obtains the single RTC-resident state.
RtcState& rtc_state();

// Prepares the RTC state for this boot. Must be called once, early in app_main,
// before any other rtc_* function. On a cold boot (power-on, or a reset that
// is not a deep-sleep wake) the block is cleared by `rtc_state_reset()`; on a
// deep-sleep wake the retained contents are preserved.
//
// Takes the board because the wake cause is a board operation (§3.8): there is
// no global board to consult, and the caller already holds one.
template<hal::BoardLike Board>
void rtc_state_init(Board const& board) {
    if (woke_from(board.sleep_get_wakeup_causes(), hal::WakeCause::kTimer)) {
        // State was retained across sleep; nothing to reset this boot.
        return;
    }
    // Cold boot (or a wake this firmware does not expect): start clean.
    rtc_state_reset(rtc_state());
}

}  // namespace thermo
