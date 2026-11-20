// Temperature serialization (§9.3) and the "no reading" sentinel (§4.1).
//
// Part of the `liblogic` component: the rounding/formatting rules and the
// `kTempNull` sentinel are application vocabulary shared by the sample buffer,
// the payload builder, and the host tests, not a property of any particular
// board. The component has no ESP-IDF dependency, so both the firmware and the
// `linux`-target tests use the same implementation.
#pragma once

// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#pragma GCC diagnostic pop
// NOLINTEND

#include <cstdint>

#include "utility/static_string.hpp"

namespace thermo::logic {

// Sentinel for "no reading" (§4.1): 0x8000.
constexpr std::int16_t kTempNull = static_cast<std::int16_t>(0x8000);

// The JSON numeric text, at most "-3276.8" (7 characters).
using TempText = utility::static_string<8>;

// §9.3 formatting: round-half-away-from-zero at one decimal place.
// Returns the JSON numeric text (e.g. "24.0", "-0.5"). Never emits a trailing
// '+' or exponent, so the result is always a valid JSON number.
TempText format_temp_c_x100(std::int16_t temp_c_x100);

}  // namespace thermo::logic
