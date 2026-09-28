// Temperature serialization (§9.3).
#include "temp_format.hpp"

#include <cstdio>

namespace thermo {

std::string format_temp_c_x100(std::int16_t temp_c_x100) {
    // §9.3 round-half-away-from-zero at one decimal place:
    //   sign = (v < 0) ? -1 : 1
    //   a    = abs(v)
    //   q    = a / 10;  r = a % 10
    //   if (r >= 5) q += 1
    //   result = sign * q
    const bool negative = temp_c_x100 < 0;
    // Use int to avoid overflow when negating a large negative int16 value.
    const int magnitude = negative ? -static_cast<int>(temp_c_x100)
                                   : static_cast<int>(temp_c_x100);
    int tenths = magnitude / 10;
    if (magnitude % 10 >= 5) {
        ++tenths;
    }
    const int value = negative ? -tenths : tenths;

    // `value` is in tenths; split into whole and fractional parts. C integer
    // division truncates toward zero, so for negative values both parts are
    // non-positive and `frac` must be negated for display.
    const int whole = value / 10;
    const int frac = value % 10;

    char buf[16];
    if (frac < 0) {
        std::snprintf(buf, sizeof(buf), "%d.%d", whole, -frac);
    } else {
        std::snprintf(buf, sizeof(buf), "%d.%d", whole, frac);
    }
    return buf;
}

} // namespace thermo
