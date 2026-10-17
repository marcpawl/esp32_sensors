// Application-wide constants: timing and limits.
// Values follow Specification.md Rev 12 (§2, §3, §4, §7).
//
// The GPIO assignment (§2.4) now lives in `hal_common` (`hal/pin_map.hpp`,
// namespace `thermo::pins`) so the firmware and the HAL `BoardPins` defaults
// share one definition; it is re-exported here for existing callers.
#pragma once

#include <cstddef>
#include <cstdint>

#include "hal/pin_map.hpp"  // thermo::pins::{kWireBus, kRedLed, ...} (§2.4)

namespace thermo::limits {

constexpr std::size_t kSensorCount = 3;  // Three DS18B20 on the shared bus.

// §4.1 record is ~12-18 bytes; §4.2 RTC slow memory yields ~450 samples.
// Caps are deliberately conservative to keep the RTC struct small.
constexpr std::size_t kMaxBufferRecords = 256;

// The DS18B20 ROM-ID byte count lives with the type in `hal_common`
// (`hal/ds18b20_types.hpp`: `thermo::hal::kRomIdBytes`).

}  // namespace thermo::limits

namespace thermo::timing {

// §7 LED semantics.
constexpr int kBootFlashMs = 200;
constexpr int kUploadOkPulseMs = 200;
constexpr int kInhibitBlinkMs = 500;
constexpr int kInhibitBlinkPeriodMs = 10000;
constexpr int kRecoveredSolidMs = 1000;

// §5.3 NTP sync retries during save.
constexpr int kNtpRetryCount = 3;
constexpr int kNtpTimeoutMs = 5000;

}  // namespace thermo::timing

namespace thermo::config {

// NVS namespace (§5.2).
constexpr const char* kNamespace = "thermo_cfg";

}  // namespace thermo::config
