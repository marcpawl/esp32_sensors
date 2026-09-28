// Tests for temperature serialization (§9.3).
//
// §9.3: round-half-away-from-zero at one decimal place, and never emit a
// trailing '+' or an exponent so the result is always a valid JSON number.
//
// Values are in hundredths of a degree Celsius (x100), matching the on-wire
// representation used by the sample buffer (§4.1).
#include "unity.h"

#include <string>

#include "temp_format.hpp"

using thermo::format_temp_c_x100;
using thermo::kTempNull;

namespace {
std::string fmt(std::int16_t v) {
    return format_temp_c_x100(v);
}
}  // namespace

// --- Exact tenths -----------------------------------------------------------

TEST_CASE("temp: exact tenths format without rounding", "[temp_format]") {
    TEST_ASSERT_EQUAL_STRING("24.0", fmt(2400).c_str());
    TEST_ASSERT_EQUAL_STRING("24.5", fmt(2450).c_str());
    TEST_ASSERT_EQUAL_STRING("0.0", fmt(0).c_str());
    TEST_ASSERT_EQUAL_STRING("-10.0", fmt(-1000).c_str());
    TEST_ASSERT_EQUAL_STRING("-0.5", fmt(-50).c_str());
}

// --- Round-half-away-from-zero (§9.3) --------------------------------------

TEST_CASE("temp: rounds half away from zero", "[temp_format]") {
    // Positive: 0.05 -> 0.1, 0.04 -> 0.0
    TEST_ASSERT_EQUAL_STRING("0.1", fmt(5).c_str());
    TEST_ASSERT_EQUAL_STRING("0.0", fmt(4).c_str());
    TEST_ASSERT_EQUAL_STRING("0.2", fmt(15).c_str());
    TEST_ASSERT_EQUAL_STRING("0.1", fmt(14).c_str());
    // Negative: -0.05 -> -0.1 (away from zero), -0.04 -> 0.0
    TEST_ASSERT_EQUAL_STRING("-0.1", fmt(-5).c_str());
    TEST_ASSERT_EQUAL_STRING("0.0", fmt(-4).c_str());
}

TEST_CASE("temp: rounding carries into the whole part", "[temp_format]") {
    // 9.95 -> 10.0, not "9.10"
    TEST_ASSERT_EQUAL_STRING("10.0", fmt(995).c_str());
    // 99.99 -> 100.0
    TEST_ASSERT_EQUAL_STRING("100.0", fmt(9999).c_str());
    // -9.95 -> -10.0
    TEST_ASSERT_EQUAL_STRING("-10.0", fmt(-995).c_str());
}

// --- Boundary values --------------------------------------------------------

TEST_CASE("temp: handles extreme int16 values", "[temp_format]") {
    // INT16_MAX = 32767 -> 327.67 -> 327.7
    TEST_ASSERT_EQUAL_STRING("327.7", fmt(32767).c_str());
    // INT16_MIN = -32768 -> -327.68 -> -327.7 (negation must not overflow)
    TEST_ASSERT_EQUAL_STRING("-327.7",
                             fmt(static_cast<std::int16_t>(-32768)).c_str());
}

TEST_CASE("temp: negative sub-tenth values keep their sign", "[temp_format]") {
    // -9 hundredths = -0.09 -> rounds away from zero to -0.1 (not "0.1",
    // which was the sign-dropping bug this case guards against).
    TEST_ASSERT_EQUAL_STRING("-0.1", fmt(-9).c_str());
    // -14 hundredths = -0.14 -> -0.1.
    TEST_ASSERT_EQUAL_STRING("-0.1", fmt(-14).c_str());
    // A negative value that rounds to zero must not render as "-0.0".
    TEST_ASSERT_EQUAL_STRING("0.0", fmt(-4).c_str());
}

// --- Realistic DS18B20 values ----------------------------------------------

TEST_CASE("temp: typical sensor readings", "[temp_format]") {
    // DS18B20 default resolution is 0.0625 C; a few real-looking samples.
    TEST_ASSERT_EQUAL_STRING("23.1", fmt(2312).c_str());  // 23.12 -> 23.1
    TEST_ASSERT_EQUAL_STRING("23.2", fmt(2315).c_str());  // 23.15 -> 23.2
    TEST_ASSERT_EQUAL_STRING("-4.4", fmt(-437).c_str());  // -4.37 -> -4.4
    TEST_ASSERT_EQUAL_STRING("85.0", fmt(8500).c_str());  // power-on default
}

// --- The null sentinel (§4.1) ----------------------------------------------

TEST_CASE("temp: null sentinel constant is 0x8000", "[temp_format]") {
    // kTempNull intentionally equals INT16_MIN and must not be treated as a
    // real temperature by callers; assert the agreed value here so a change
    // breaks the build loudly.
    TEST_ASSERT_EQUAL_INT16(static_cast<std::int16_t>(0x8000), kTempNull);
    TEST_ASSERT_EQUAL_INT16(-32768, kTempNull);
}
