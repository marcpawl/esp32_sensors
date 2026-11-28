// Tests for the mode-sense pole mapping (`logic/mode_selection.hpp`,
// §3.1-§3.3).
//
// The board exposes the 3-position ON-OFF-ON switch as two independent
// switches — one per mode-sense pole — so the DPDT interpretation is logic. It
// lives in a pure, `constexpr`, ESP-IDF-free header in the `liblogic` component
// precisely so it can be tested here without hardware: the stub board's `set()`
// is not needed, the pole states are passed in directly.
//
// All four combinations are covered: left (Configure), right (Run), center
// (Off), and both-on (`Mode::kInvalid`, from a bouncing or miswired switch).
// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#pragma GCC diagnostic pop
// NOLINTEND

#include "logic/mode.hpp"
#include "logic/mode_selection.hpp"
#include "unity.h"

namespace {

using thermo::espidf_cpp::SwitchState;

constexpr SwitchState kOn = SwitchState::kOn;
constexpr SwitchState kOff = SwitchState::kOff;

}  // namespace

// --- Healthy switch positions ------------------------------------------------

TEST_CASE("mode: configure pole alone selects Configure", "[mode_selection]") {
    const thermo::logic::Mode mode =
        thermo::logic::mode_from_switches(kOn, kOff);
    TEST_ASSERT_EQUAL(thermo::logic::Mode::kConfigure, mode);
}

TEST_CASE("mode: run pole alone selects Run", "[mode_selection]") {
    const thermo::logic::Mode mode =
        thermo::logic::mode_from_switches(kOff, kOn);
    TEST_ASSERT_EQUAL(thermo::logic::Mode::kRun, mode);
}

TEST_CASE("mode: neither pole selects Off (center position)",
          "[mode_selection]") {
    // The center position cuts power, so firmware observes it only
    // transiently; it is a valid state, not an error.
    const thermo::logic::Mode mode =
        thermo::logic::mode_from_switches(kOff, kOff);
    TEST_ASSERT_EQUAL(thermo::logic::Mode::kOff, mode);
}

// --- Invalid pole pair -------------------------------------------------------

TEST_CASE("mode: both poles on reports Invalid", "[mode_selection]") {
    // A bouncing or miswired switch energizes both sense poles, so the mapping
    // reports an explicit invalid mode instead of guessing.
    TEST_ASSERT_EQUAL(thermo::logic::Mode::kInvalid,
                      thermo::logic::mode_from_switches(kOn, kOn));
}

// --- Contract ----------------------------------------------------------------

TEST_CASE("mode: mapping is constexpr", "[mode_selection]") {
    // The mapping must stay usable at compile time (it is pure), which keeps
    // it free of logging, allocation, and reboot side effects.
    static_assert(thermo::logic::mode_from_switches(SwitchState::kOn,
                                                    SwitchState::kOff) ==
                  thermo::logic::Mode::kConfigure);
    static_assert(thermo::logic::mode_from_switches(SwitchState::kOff,
                                                    SwitchState::kOn) ==
                  thermo::logic::Mode::kRun);
    static_assert(thermo::logic::mode_from_switches(SwitchState::kOff,
                                                    SwitchState::kOff) ==
                  thermo::logic::Mode::kOff);
    static_assert(
        thermo::logic::mode_from_switches(SwitchState::kOn, SwitchState::kOn) ==
        thermo::logic::Mode::kInvalid);
    TEST_ASSERT_TRUE(true);  // The assertions above are the test.
}

TEST_CASE("mode: to_string names every mode", "[mode_selection]") {
    // `mode_from_switches()` returns these values, so each must be printable
    // for the boot log.
    TEST_ASSERT_EQUAL_STRING(
        "Off", thermo::logic::to_string(thermo::logic::Mode::kOff));
    TEST_ASSERT_EQUAL_STRING(
        "Configure", thermo::logic::to_string(thermo::logic::Mode::kConfigure));
    TEST_ASSERT_EQUAL_STRING(
        "Run", thermo::logic::to_string(thermo::logic::Mode::kRun));
    TEST_ASSERT_EQUAL_STRING(
        "Invalid", thermo::logic::to_string(thermo::logic::Mode::kInvalid));
}
