// Fixed capacities shared by the sample buffer, the payload, and the sensor
// layer (§2.1, §4.1, §4.2).
//
// Part of the `logic` component: how many sensors the device logs and how many
// samples the RTC buffer holds are properties of the application, not of any
// particular board, and both the firmware and the host tests must agree on
// them. The header is deliberately dependency-free (only `<cstddef>`) so it
// can be included by module state that must not pull in a board or an ESP-IDF
// driver.
//
// The GPIO assignment is a different kind of constant and lives with the HAL
// vocabulary (`hal/pin_map.hpp`, namespace `thermo::pins`).
#pragma once

// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#pragma GCC diagnostic pop
// NOLINTEND

#include <cstddef>

namespace thermo::limits {

constexpr std::size_t kSensorCount = 3;  // Three DS18B20 on the shared bus.

// §4.1 record is ~12-18 bytes; §4.2 RTC slow memory yields ~450 samples.
// Caps are deliberately conservative to keep the RTC struct small.
constexpr std::size_t kMaxBufferRecords = 256;

// The DS18B20 ROM-ID byte count lives with the type in `hal_common`
// (`hal/ds18b20_types.hpp`: `thermo::hal::kRomIdBytes`).

}  // namespace thermo::limits
