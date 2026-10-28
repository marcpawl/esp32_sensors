// Tests for the mode-sense pole mapping (`mode_selection.hpp`, §3.1-§3.3).
//
// The board exposes the 3-position ON-OFF-ON switch as two independent push
// buttons — one per mode-sense pole — so the DPDT interpretation is logic. It
// lives in a pure, `constexpr`, ESP-IDF-free header precisely so it can be
// tested here without hardware: the stub board's `set()` is not needed, the
// pole states are passed in directly.
//
// A healthy switch energizes at most one sense pole, so every one of the four
// combinations is covered: left (Configure), right (Run), center (Off), and
// both-on (the miswired/shorted hardware fault, reported as `std::nullopt`).
// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#pragma GCC diagnostic pop
// NOLINTEND

#include "mode.hpp"
#include "mode_selection.hpp"
#include "unity.h"

namespace {

using thermo::hal::ButtonState;

constexpr ButtonState kOn = ButtonState::kOn;
constexpr ButtonState kOff = ButtonState::kOff;

}  // namespace

// --- Healthy switch positions ------------------------------------------------

TEST_CASE("mode: configure pole alone selects Configure", "[mode_selection]") {
    const thermo::ModeSelection mode = thermo::mode_from_buttons(kOn, kOff);
    TEST_ASSERT_TRUE(mode.has_value());
    TEST_ASSERT_EQUAL(thermo::Mode::kConfigure, *mode);
}

TEST_CASE("mode: run pole alone selects Run", "[mode_selection]") {
    const thermo::ModeSelection mode = thermo::mode_from_buttons(kOff, kOn);
    TEST_ASSERT_TRUE(mode.has_value());
    TEST_ASSERT_EQUAL(thermo::Mode::kRun, *mode);
}

TEST_CASE("mode: neither pole selects Off (center position)",
          "[mode_selection]") {
    // The center position cuts power, so firmware observes it only
    // transiently; it is a valid state, not an error.
    const thermo::ModeSelection mode = thermo::mode_from_buttons(kOff, kOff);
    TEST_ASSERT_TRUE(mode.has_value());
    TEST_ASSERT_EQUAL(thermo::Mode::kOff, *mode);
}

// --- Hardware fault ----------------------------------------------------------

TEST_CASE("mode: both poles on reports a fault", "[mode_selection]") {
    // Miswired or shorted switch: impossible for a healthy DPDT, so the
    // mapping refuses to guess a mode.
    TEST_ASSERT_FALSE(thermo::mode_from_buttons(kOn, kOn).has_value());
}

// --- Contract ----------------------------------------------------------------

TEST_CASE("mode: mapping is constexpr", "[mode_selection]") {
    // The mapping must stay usable at compile time (it is pure), which keeps
    // it free of logging, allocation, and reboot side effects.
    static_assert(thermo::mode_from_buttons(ButtonState::kOn, ButtonState::kOff)
                      .value() == thermo::Mode::kConfigure);
    static_assert(thermo::mode_from_buttons(ButtonState::kOff, ButtonState::kOn)
                      .value() == thermo::Mode::kRun);
    static_assert(
        thermo::mode_from_buttons(ButtonState::kOff, ButtonState::kOff)
            .value() == thermo::Mode::kOff);
    static_assert(!thermo::mode_from_buttons(ButtonState::kOn, ButtonState::kOn)
                       .has_value());
    TEST_ASSERT_TRUE(true);  // The assertions above are the test.
}

TEST_CASE("mode: to_string names every mode", "[mode_selection]") {
    // `mode_from_buttons()` returns these values, so each must be printable
    // for the boot log.
    TEST_ASSERT_EQUAL_STRING("Off", thermo::to_string(thermo::Mode::kOff));
    TEST_ASSERT_EQUAL_STRING("Configure",
                             thermo::to_string(thermo::Mode::kConfigure));
    TEST_ASSERT_EQUAL_STRING("Run", thermo::to_string(thermo::Mode::kRun));
}
