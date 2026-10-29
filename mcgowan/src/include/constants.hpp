// Application-wide constants: timing and the NVS namespace.
// Values follow Specification.md Rev 13 (§3, §5, §7).
//
// The GPIO assignment (§2.4) lives in `hal_common` (`hal/pin_map.hpp`,
// namespace `thermo::pins`) so the firmware and the HAL `BoardPins` defaults
// share one definition. It is re-exported here because several firmware
// sources (`battery_monitor.cpp`, `configure_mode.cpp`, `run_mode.cpp`) reach
// `thermo::pins::` through this header.
//
// The fixed capacities (`thermo::limits::kSensorCount`,
// `thermo::limits::kMaxBufferRecords`) are board-independent and shared with
// the host tests, so they live in the `logic` component (`logic/limits.hpp`).
#pragma once

// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#pragma GCC diagnostic pop
// NOLINTEND

#include <cstdint>

#include "hal/pin_map.hpp"  // thermo::pins::{kWireBus, kRedLed, ...} (§2.4)

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
