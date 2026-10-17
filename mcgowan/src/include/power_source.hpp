// Power-source enumeration (§2.2, Rev 13).
//
// The enum and its name mapping are deliberately kept free of any IDF or
// hardware dependency so they can be used (and unit-tested) on the host.
#pragma once

#include <cstdint>

namespace thermo {

// Where the ESP32 is currently drawing power from (§2.2).
enum class PowerSource : std::uint8_t {
    kUnknown = 0,
    kSolarBattery,    // Solar-powered battery pack is installed and powering.
    kUsbBatteryPack,  // USB port, dumb battery pack (VBUS only, no host).
    kUsbComputer,     // USB port, host computer (enumerated data lines).
};

// Human-readable name for a power source, e.g. for logging or the /scan JSON.
// The returned pointer is to a string literal with static storage duration.
const char* to_string(PowerSource source) noexcept;

}  // namespace thermo
