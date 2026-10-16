// Tests for the stub LED (`hal_stub`, §2.1, §7).
//
// The LED HAL is deliberately minimal: `Led::write()` takes the requested
// `LedState` and the stub records it, logging "<name>: On" / "<name>: Off".
// These tests cover the *stub device* contract — construction, `init()`, the
// default state, round-trips through `write()`, `state()`, and the shared
// `LedLike` concept. They run on the ESP-IDF `linux` target with Unity and
// link the pin-free `hal_stub` component (never `hal_board`).
//
// The stub holds its state in software, so every case is deterministic and
// needs no hardware.
#include "unity.h"

#include <concepts>

#include "hal/led.hpp"
#include "hal/led_types.hpp"

namespace {

// Convenience aliases for the shared HAL vocabulary.
using thermo::hal::Led;
using thermo::hal::LedState;

// Pin numbers are irrelevant to the stub, but the factory takes them for
// interface parity with the GPIO-backed implementation.
constexpr int kGreenGpio = 17;

// The label is used only for the stub's log output; a string literal outlives
// the object.
constexpr const char* kGreenName = "Green";

Led make_led(const char* name = kGreenName) {
    Led led = Led::create(kGreenGpio, name);
    led.init();
    return led;
}

}  // namespace

// --- Construction / factory -------------------------------------------------

TEST_CASE("hal led: create() returns a usable, inited led", "[hal_led]") {
    Led led = Led::create(kGreenGpio, kGreenName);
    // init() always succeeds for the stub (there is no pin to configure).
    TEST_ASSERT_EQUAL(ESP_OK, led.init());
    // A freshly created LED starts off.
    TEST_ASSERT_EQUAL(static_cast<int>(LedState::kOff),
                      static_cast<int>(led.state()));
}

TEST_CASE("hal led: init() is idempotent and returns ESP_OK", "[hal_led]") {
    Led led = make_led();
    TEST_ASSERT_EQUAL(ESP_OK, led.init());
    TEST_ASSERT_EQUAL(ESP_OK, led.init());
}

// --- Default state ----------------------------------------------------------

TEST_CASE("hal led: defaults to off", "[hal_led]") {
    const Led led = make_led();
    TEST_ASSERT_EQUAL(static_cast<int>(LedState::kOff),
                      static_cast<int>(led.state()));
}

// --- Round-trips through write() -------------------------------------------

TEST_CASE("hal led: write() is reflected by state()", "[hal_led]") {
    Led led = make_led();

    led.write(LedState::kOn);
    TEST_ASSERT_EQUAL(static_cast<int>(LedState::kOn),
                      static_cast<int>(led.state()));

    led.write(LedState::kOff);
    TEST_ASSERT_EQUAL(static_cast<int>(LedState::kOff),
                      static_cast<int>(led.state()));
}

TEST_CASE("hal led: repeated write() keeps the last value", "[hal_led]") {
    Led led = make_led();
    led.write(LedState::kOn);
    led.write(LedState::kOff);
    led.write(LedState::kOn);  // last write wins
    TEST_ASSERT_EQUAL(static_cast<int>(LedState::kOn),
                      static_cast<int>(led.state()));
}

TEST_CASE("hal led: write() to the same state twice is stable", "[hal_led]") {
    Led led = make_led();
    led.write(LedState::kOn);
    led.write(LedState::kOn);
    TEST_ASSERT_EQUAL(static_cast<int>(LedState::kOn),
                      static_cast<int>(led.state()));
}

// --- The name is retained for logging --------------------------------------

TEST_CASE("hal led: the name is accepted and usable for logging", "[hal_led]") {
    // The stub logs "<name>: On"/"<name>: Off"; the name is only observable
    // through the log, so the contract we can assert here is that a named LED
    // behaves like any other. Writing exercises the logging path (which must
    // not crash on a string-literal label).
    Led led = make_led("Red");
    led.write(LedState::kOn);
    TEST_ASSERT_EQUAL(static_cast<int>(LedState::kOn),
                      static_cast<int>(led.state()));
    led.write(LedState::kOff);
    TEST_ASSERT_EQUAL(static_cast<int>(LedState::kOff),
                      static_cast<int>(led.state()));
}

// --- The stub satisfies the shared concept ---------------------------------

TEST_CASE("hal led: stub Led models LedLike", "[hal_led]") {
    static_assert(thermo::hal::LedLike<Led>);
    // create() must return the same concrete type (part of the concept).
    static_assert(std::same_as<decltype(Led::create(1, "x")), Led>);
    TEST_ASSERT_EQUAL(ESP_OK, make_led().init());
}
