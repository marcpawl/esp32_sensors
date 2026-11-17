// Fixed capacities shared by the sample buffer, the payload, and the sensor
// layer (§2.1, §4.1, §4.2).
//
// Part of the `liblogic` component. The sensor count is defined by the shared
// `hal_common` vocabulary, so the bus and the application cannot disagree;
// the RTC buffer capacity is an application property. Both the firmware and
// the host tests use these names, and the header pulls in no board or
// ESP-IDF driver.
//
// The GPIO assignment is a different kind of constant and lives with the HAL
// vocabulary (`hal/pin_map.hpp`, namespace `thermo::pins`).
#pragma once

// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#pragma GCC diagnostic pop
// NOLINTEND

#include <cstddef>

#include "hal/ds18b20_types.hpp"  // kMaxSensors

namespace thermo::limits {

// The application logs every sensor the shared bus can carry (§2.1). The
// bus capacity is the single definition; this name is the application's
// view of it, so the two can never disagree.
constexpr std::size_t kSensorCount = hal::kMaxSensors;

// Longest friendly sensor name the config form accepts (§5.1.3). The
// payload keys are built from it, so the name is held in a fixed-capacity
// string rather than a std::string.
constexpr std::size_t kMaxSensorNameLength = 32;

// Longest device name the config form accepts (§5.1). An application rule,
// not a HAL or ESP-IDF limit: the name is stored in NVS and sent as the
// payload's "device" key, never passed to an ESP-IDF API.
constexpr std::size_t kMaxDeviceNameLength = 32;

// Fixed capacity of one upload payload (§9.1). The firmware formats the
// document on its stack, so this stays small: a batch that does not fit
// aborts. That is a bad state — a large batch means many samples could not
// be transmitted yet, and the abort loses them with the RTC buffer.
constexpr std::size_t kMaxPayloadBytes = 1024;

// §4.1 record is ~12-18 bytes; §4.2 RTC slow memory yields ~450 samples.
// Caps are deliberately conservative to keep the RTC struct small.
constexpr std::size_t kMaxBufferRecords = 256;

// The DS18B20 ROM-ID byte count lives with the type in `hal_common`
// (`hal/ds18b20_types.hpp`: `thermo::hal::kRomIdBytes`).

}  // namespace thermo::limits
