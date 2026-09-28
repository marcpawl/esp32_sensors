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

    // `value` is in tenths; split into magnitude parts. C integer division
    // truncates toward zero, so work on the absolute value and emit the sign
    // explicitly: for -1 <= value <= 0 the whole part is 0, and relying on the
    // sign of `value % 10` would drop the '-' (e.g. -0.5 -> "0.5").
    const int abs_value = (value < 0) ? -value : value;
    const int whole = abs_value / 10;
    const int frac = abs_value % 10;

    // A negative input that rounds to zero must render as "0.0", not "-0.0".
    const bool emit_sign = negative && abs_value != 0;

    char buf[16];
    std::snprintf(buf, sizeof(buf), "%s%d.%d", emit_sign ? "-" : "", whole,
                  frac);
    return buf;
}

}  // namespace thermo
