// LED status indication vocabulary (§7).
//
// Part of the `logic` component: `TxState` is the transmission gate derived
// from the battery high-water / low-water hysteresis (§3.6), which is a
// property of the application, not of any particular board. It therefore lives
// here (as `logic/led_manager.hpp`) so the firmware, the host tests, and the
// RTC state all share one definition.
//
// The header is deliberately dependency-free (only `<cstdint>`), so it can be
// included by module state that must not pull in a board or an ESP-IDF driver.
#pragma once

// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#pragma GCC diagnostic pop
// NOLINTEND

#include <cstdint>

namespace thermo {

enum class TxState : std::uint8_t {
    kTxOk = 0,         // Transmission permitted.
    kTxInhibited = 1,  // Low battery; buffering only.
};

}  // namespace thermo
