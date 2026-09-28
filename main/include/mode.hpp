// Operating modes selected by the 3-position ON-OFF-ON switch (§3).
#pragma once

#include <cstdint>

namespace thermo {

enum class Mode : std::uint8_t {
    kOff = 0,       // Center position; power is cut by the switch, so this is
                    // only observable transiently.
    kConfigure = 1, // Left position: HTTPS config server.
    kRun = 2,       // Right position: sample/upload/deep-sleep loop.
};

constexpr const char* to_string(Mode mode) {
    switch (mode) {
    case Mode::kOff:
        return "Off";
    case Mode::kConfigure:
        return "Configure";
    case Mode::kRun:
        return "Run";
    }
    return "Unknown";
}

} // namespace thermo
