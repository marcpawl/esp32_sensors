// Operating modes selected by the 3-position ON-OFF-ON switch (§3).
//
// Part of the `liblogic` component: `Mode` describes the application's
// operating point, not any particular board, so the firmware, the host tests,
// and any other consumer share one definition.
//
// The switch itself is not a HAL device: the board exposes its two mode-sense
// poles as independent switches, and `logic/mode_selection.hpp` maps that
// pair onto these values. Both poles on at once has no healthy switch position,
// so it maps to `kInvalid` (a bounce or a wiring fault).
#pragma once

// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#pragma GCC diagnostic pop
// NOLINTEND

#include <cstdint>

namespace thermo::logic {

enum class Mode : std::uint8_t {
    kOff = 0,        // Center position; power is cut by the switch, so this is
                     // only observable transiently.
    kConfigure = 1,  // Left position: HTTPS config server.
    kRun = 2,        // Right position: sample/upload/deep-sleep loop.
    kInvalid = 3,    // Both sense poles on: switch bouncing or a wiring fault.
};

constexpr const char* to_string(Mode mode)
{
    switch (mode) {
        case Mode::kOff:
            return "Off";
        case Mode::kConfigure:
            return "Configure";
        case Mode::kRun:
            return "Run";
        case Mode::kInvalid:
            return "Invalid";
    }
    return "Unknown";
}

}  // namespace thermo::logic
