// Application-wide constants: GPIO assignments, timing, and limits.
// Values follow Specification.md Rev 12 (§2, §3, §4, §7).
#pragma once

#include <cstddef>
#include <cstdint>

namespace thermo::pins {

// §2.4 GPIO assignment.
constexpr int kWireBus = 4;       // 1-Wire bus (DS18B20 x3 + 4.7k pull-up)
constexpr int kRedLed = 16;       // Red LED
constexpr int kGreenLed = 17;     // Green LED
constexpr int kConfig = 18;       // Mode sense: Configure (HIGH == configure)
constexpr int kRun = 19;          // Mode sense: Run (HIGH == run)
constexpr int kBatteryAdc = 34;   // Battery voltage ADC (via divider)
constexpr int kDividerGate = 25;  // Optional divider gate (P-MOSFET control)

}  // namespace thermo::pins

namespace thermo::limits {

constexpr std::size_t kSensorCount = 3;  // Three DS18B20 on the shared bus.

// §4.1 record is ~12-18 bytes; §4.2 RTC slow memory yields ~450 samples.
// Caps are deliberately conservative to keep the RTC struct small.
constexpr std::size_t kMaxBufferRecords = 256;

// Sensor ROM IDs are 8 bytes each.
constexpr std::size_t kRomIdBytes = 8;

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
