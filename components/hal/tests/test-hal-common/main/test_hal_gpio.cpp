// Tests for the GPIO strong type (`hal/gpio.hpp`) and the typed pin map
// (`hal/pin_map.hpp`).
//
// The type exists so a pin number cannot be confused with another number in
// the HAL API. Its tests pin what makes that true: the constructor is
// `explicit`, there is no implicit conversion out, the value is readable only
// through `as_int()`, and the whole type is usable in constant expressions.
// The pin map is checked on the same footing: every §2.4 constant carries the
// type, so `BoardPins` defaults stay type-safe.
// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#pragma GCC diagnostic pop
// NOLINTEND

#include <concepts>
#include <type_traits>

#include "hal/gpio.hpp"
#include "hal/pin_map.hpp"
#include "unity.h"

namespace {

using thermo::hal::Gpio;

}  // namespace

// --- Construction and reading back -----------------------------------------

TEST_CASE("hal gpio: a default-constructed pin is GPIO 0", "[hal_gpio]")
{
    // `BoardPins` relies on this for the one pin the reference board leaves
    // unwired (the plain user switch), so the default is part of the contract
    // and must be constant-evaluated.
    constexpr Gpio pin{};
    static_assert(pin.as_int() == 0);
    TEST_ASSERT_EQUAL(0, pin.as_int());
}

TEST_CASE("hal gpio: as_int() returns the constructed pin", "[hal_gpio]")
{
    // The constructor and the accessor are `constexpr`, so a pin is usable
    // wherever an `int` would be (static tables, template arguments).
    static_assert(Gpio{4}.as_int() == 4);
    TEST_ASSERT_EQUAL(4, Gpio{4}.as_int());
    TEST_ASSERT_EQUAL(34, Gpio{34}.as_int());
}

TEST_CASE("hal gpio: pins compare by value", "[hal_gpio]")
{
    static_assert(Gpio{4} == Gpio{4});
    static_assert(!(Gpio{4} == Gpio{5}));
    TEST_ASSERT_TRUE(Gpio{4} == Gpio{4});
    TEST_ASSERT_FALSE(Gpio{4} == Gpio{5});
}

// --- No accidental conversion either way -----------------------------------

TEST_CASE("hal gpio: the conversion is explicit in both directions",
          "[hal_gpio]")
{
    // An `int` must not turn into a pin implicitly (that is the mistake the
    // type exists to prevent), and a pin must not decay back to an `int`.
    static_assert(!std::is_convertible_v<int, Gpio>);
    static_assert(!std::is_convertible_v<Gpio, int>);
    // Explicit construction is the one way in, and `as_int()` the one way out.
    static_assert(std::is_constructible_v<Gpio, int>);
    static_assert(!std::is_assignable_v<Gpio&, int>);
    TEST_PASS();
}

TEST_CASE("hal gpio: a pin is a cheap value type", "[hal_gpio]")
{
    // It is copied into every device and every `BoardPins`, so it must stay a
    // trivially copyable wrapper around one number.
    static_assert(std::is_trivially_copyable_v<Gpio>);
    static_assert(sizeof(Gpio) == sizeof(int));
    TEST_PASS();
}

// --- The pin map carries the type ------------------------------------------

TEST_CASE("hal gpio: every pin-map constant is a Gpio", "[hal_gpio]")
{
    // The §2.4 map is the single definition of the wiring; typing it means
    // `BoardPins` defaults cannot pick up a bare `int`.
    static_assert(
        std::same_as<std::remove_cv_t<decltype(thermo::pins::kWireBus)>, Gpio>);
    static_assert(
        std::same_as<std::remove_cv_t<decltype(thermo::pins::kRedLed)>, Gpio>);
    static_assert(
        std::same_as<std::remove_cv_t<decltype(thermo::pins::kGreenLed)>,
                     Gpio>);
    static_assert(
        std::same_as<std::remove_cv_t<decltype(thermo::pins::kConfig)>, Gpio>);
    static_assert(
        std::same_as<std::remove_cv_t<decltype(thermo::pins::kRun)>, Gpio>);
    static_assert(
        std::same_as<std::remove_cv_t<decltype(thermo::pins::kBatteryAdc)>,
                     Gpio>);
    static_assert(
        std::same_as<std::remove_cv_t<decltype(thermo::pins::kDividerGate)>,
                     Gpio>);
    TEST_PASS();
}

TEST_CASE("hal gpio: the pin-map constants keep their §2.4 values",
          "[hal_gpio]")
{
    // The numbers themselves are the specification's; this pins them once,
    // next to the type change that made the map easy to read.
    TEST_ASSERT_EQUAL(4, thermo::pins::kWireBus.as_int());
    TEST_ASSERT_EQUAL(16, thermo::pins::kRedLed.as_int());
    TEST_ASSERT_EQUAL(17, thermo::pins::kGreenLed.as_int());
    TEST_ASSERT_EQUAL(1, thermo::pins::kConfig.as_int());
    TEST_ASSERT_EQUAL(2, thermo::pins::kRun.as_int());
    TEST_ASSERT_EQUAL(34, thermo::pins::kBatteryAdc.as_int());
    TEST_ASSERT_EQUAL(25, thermo::pins::kDividerGate.as_int());
}
