// The single `RTC_DATA_ATTR` state block (§8).
//
// Only the storage lives here: `logic/rtc_state.hpp` defines the block's shape
// and the buffer operations, and `rtc_storage.hpp` derives the per-boot
// initialization from the board's wake cause. Keeping the `RTC_DATA_ATTR`
// declaration in the firmware is what lets the rest be host-testable — on the
// `linux` target `RTC_DATA_ATTR` expands to a static assert that fails the
// build.
#include "rtc_storage.hpp"
// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#include "esp_attr.h"
#pragma GCC diagnostic pop

// NOLINTEND

namespace thermo {
namespace {

// The single RTC-resident state block. RTC_DATA_ATTR places it in RTC slow
// memory, which is retained across deep sleep.
RTC_DATA_ATTR RtcState g_rtc_state;

}  // namespace

RtcState& rtc_state() {
    return g_rtc_state;
}

// NOTE: rtc_state_init() is a template over `BoardLike`, so it is defined in
// rtc_storage.hpp. The buffer operations and the cold-boot reset live in the
// `logic` component (`logic/rtc_state.hpp`, `logic/src/rtc_buffer.cpp`).

}  // namespace thermo
