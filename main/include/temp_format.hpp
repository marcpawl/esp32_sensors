// Temperature serialization (§9.3).
//
// Kept free of ESP-IDF dependencies so the rounding and formatting rules can
// be unit-tested on the host target.
#pragma once

#include <cstdint>
#include <string>

namespace thermo {

// Sentinel for "no reading" (§4.1): 0x8000.
constexpr std::int16_t kTempNull = static_cast<std::int16_t>(0x8000);

// §9.3 formatting: round-half-away-from-zero at one decimal place.
// Returns the JSON numeric text (e.g. "24.0", "-0.5"). Never emits a trailing
// '+' or exponent, so the result is always a valid JSON number.
std::string format_temp_c_x100(std::int16_t temp_c_x100);

} // namespace thermo
