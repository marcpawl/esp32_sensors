// Power-source name mapping (§2.2, Rev 13).
//
// Kept hardware-free so it links into host-based unit tests.
#include "power_source.hpp"

namespace thermo {

const char* to_string(PowerSource source) noexcept {
    switch (source) {
        case PowerSource::kSolarBattery:
            return "solar battery";
        case PowerSource::kUsbBatteryPack:
            return "USB battery pack";
        case PowerSource::kUsbComputer:
            return "USB computer";
        case PowerSource::kUnknown:
            return "Unknown";
    }
    return "unknown";
}

}  // namespace thermo
