// Temperature serialization (§9.3) and the "no reading" sentinel (§4.1).
//
// Part of the `logic` component: the rounding/formatting rules and the
// `kTempNull` sentinel are application vocabulary shared by the sample buffer,
// the payload builder, and the host tests, not a property of any particular
// board. The component has no ESP-IDF dependency, so both the firmware and the
// `linux`-target tests use the same implementation.
#pragma once

// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#pragma GCC diagnostic pop
// NOLINTEND

#include <string>

#include <cstdint>

namespace thermo {

// Sentinel for "no reading" (§4.1): 0x8000.
constexpr std::int16_t kTempNull = static_cast<std::int16_t>(0x8000);

// §9.3 formatting: round-half-away-from-zero at one decimal place.
// Returns the JSON numeric text (e.g. "24.0", "-0.5"). Never emits a trailing
// '+' or exponent, so the result is always a valid JSON number.
std::string format_temp_c_x100(std::int16_t temp_c_x100);

}  // namespace thermo
