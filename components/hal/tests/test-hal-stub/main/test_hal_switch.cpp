// Tests for the stub switch (`hal_stub`).
//
// The switch HAL is deliberately semantics-free: `Switch::read()` returns
// whether the switch is currently closed as a `SwitchState`, `gpio()` reports
// the pin, and interpreting a transition (edge detection, debouncing, hold
// timing) is the caller's job. These tests therefore cover the *stub device*
// contract — construction, `init()`, the default reading, round-trips through
// `set()`, the interrupt callback registered with `set_interrupt()`, and the
// shared `SwitchLike` concept.
// They run on the ESP-IDF `linux` target with Unity and link the pin-free
// `hal_stub` component (never `hal_board`).
//
// The stub holds its reported state in software, so every case is
// deterministic and needs no hardware. The stub has no real interrupt, so
// `set_interrupt()` callbacks are invoked synchronously from `set()`. The
// callback takes no argument, like the hardware ISR entry point: it reads the
// new state through `read()`.
// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#pragma GCC diagnostic pop
// NOLINTEND

#include <concepts>

#include "hal/switch_types.hpp"
#include "hal_stub/switch.hpp"
#include "unity.h"

namespace {

// Convenience aliases for the shared HAL vocabulary.
using thermo::hal::SwitchState;
using thermo::hal::stub::Switch;

// Pin numbers are irrelevant to the stub's state, but the factory takes them
// for interface parity with the GPIO-backed implementation, and `gpio()`
// reports them back.
constexpr int kSwitchGpio = 0;

Switch make_switch(int gpio = kSwitchGpio) {
    Switch b = Switch::create(gpio);
    b.init();
    return b;
}

// Records the calls a registered callback receives, so tests can assert on the
// sequence of state changes. The callback reads the switch itself, so the log
// is filled by the lambda the test registers.
struct CallbackLog {
    int calls = 0;
    // Number of closes / opens observed, useful for edge assertions.
    int closes = 0;
    int opens = 0;
};

}  // namespace

// --- Construction / factory -------------------------------------------------

TEST_CASE("hal switch: create() returns a usable, inited switch",
          "[hal_switch]") {
    Switch b = Switch::create(kSwitchGpio);
    // init() always succeeds for the stub (there is no pin to configure).
    TEST_ASSERT_EQUAL(ESP_OK, b.init());
    // A freshly created switch reads as open.
    TEST_ASSERT_EQUAL(static_cast<int>(SwitchState::kOff),
                      static_cast<int>(b.read()));
}

TEST_CASE("hal switch: init() is idempotent and returns ESP_OK",
          "[hal_switch]") {
    Switch b = make_switch();
    TEST_ASSERT_EQUAL(ESP_OK, b.init());
    TEST_ASSERT_EQUAL(ESP_OK, b.init());
}

// --- The pin is reported back -----------------------------------------------

TEST_CASE("hal switch: gpio() reports the constructed pin", "[hal_switch]") {
    const Switch a = Switch::create(7);
    TEST_ASSERT_EQUAL(7, a.gpio());

    const Switch b = Switch::create(42);
    TEST_ASSERT_EQUAL(42, b.gpio());

    // Part of the concept, so a consumer can name the pin in a diagnostic.
    static_assert(
        std::same_as<decltype(std::declval<Switch const&>().gpio()), int>);
}

// --- Default reading --------------------------------------------------------

TEST_CASE("hal switch: defaults to open", "[hal_switch]") {
    const Switch b = make_switch();
    TEST_ASSERT_EQUAL(static_cast<int>(SwitchState::kOff),
                      static_cast<int>(b.read()));
}

// --- Round-trips through set() ---------------------------------------------

TEST_CASE("hal switch: set() is reflected by read()", "[hal_switch]") {
    Switch b = make_switch();

    b.set(SwitchState::kOn);
    TEST_ASSERT_EQUAL(static_cast<int>(SwitchState::kOn),
                      static_cast<int>(b.read()));

    b.set(SwitchState::kOff);
    TEST_ASSERT_EQUAL(static_cast<int>(SwitchState::kOff),
                      static_cast<int>(b.read()));
}

TEST_CASE("hal switch: repeated set() keeps the last value", "[hal_switch]") {
    Switch b = make_switch();
    b.set(SwitchState::kOn);
    b.set(SwitchState::kOff);
    b.set(SwitchState::kOn);  // last write wins
    TEST_ASSERT_EQUAL(static_cast<int>(SwitchState::kOn),
                      static_cast<int>(b.read()));
}

TEST_CASE("hal switch: setting the same state twice is stable",
          "[hal_switch]") {
    Switch b = make_switch();
    b.set(SwitchState::kOn);
    b.set(SwitchState::kOn);
    TEST_ASSERT_EQUAL(static_cast<int>(SwitchState::kOn),
                      static_cast<int>(b.read()));
}

// --- Interrupt callback -----------------------------------------------------

TEST_CASE("hal switch: no callback by default", "[hal_switch]") {
    // Without set_interrupt(), a state change calls nothing and must not crash.
    Switch b = make_switch();
    b.set(SwitchState::kOn);
    b.set(SwitchState::kOff);
    TEST_PASS();
}

TEST_CASE("hal switch: set_interrupt() fires on a state change",
          "[hal_switch]") {
    Switch b = make_switch();
    int calls = 0;
    SwitchState last = SwitchState::kOff;
    b.set_interrupt([&b, &calls, &last] {
        ++calls;
        last = b.read();
    });

    b.set(SwitchState::kOn);
    TEST_ASSERT_EQUAL(1, calls);
    TEST_ASSERT_EQUAL(static_cast<int>(SwitchState::kOn),
                      static_cast<int>(last));

    b.set(SwitchState::kOff);
    TEST_ASSERT_EQUAL(2, calls);
    TEST_ASSERT_EQUAL(static_cast<int>(SwitchState::kOff),
                      static_cast<int>(last));
}

TEST_CASE("hal switch: callback is not fired without a state change",
          "[hal_switch]") {
    Switch b = make_switch();
    int calls = 0;
    b.set_interrupt([&calls] { ++calls; });

    b.set(SwitchState::kOn);
    b.set(SwitchState::kOn);  // unchanged: no second notification
    TEST_ASSERT_EQUAL(1, calls);

    b.set(SwitchState::kOff);
    b.set(SwitchState::kOff);  // unchanged: no second notification
    TEST_ASSERT_EQUAL(2, calls);
}

TEST_CASE("hal switch: callback reports the close/open sequence",
          "[hal_switch]") {
    Switch b = make_switch();
    CallbackLog log;
    b.set_interrupt([&b, &log] {
        ++log.calls;
        if (b.read() == SwitchState::kOn) {
            ++log.closes;
        } else {
            ++log.opens;
        }
    });

    b.set(SwitchState::kOn);
    b.set(SwitchState::kOff);
    b.set(SwitchState::kOn);

    TEST_ASSERT_EQUAL(3, log.calls);
    TEST_ASSERT_EQUAL(2, log.closes);
    TEST_ASSERT_EQUAL(1, log.opens);
}

TEST_CASE("hal switch: set_interrupt({}) unregisters the callback",
          "[hal_switch]") {
    Switch b = make_switch();
    int calls = 0;
    b.set_interrupt([&calls] { ++calls; });
    b.set(SwitchState::kOn);
    TEST_ASSERT_EQUAL(1, calls);

    // Clear the callback: subsequent changes are silent.
    b.set_interrupt({});
    b.set(SwitchState::kOff);
    b.set(SwitchState::kOn);
    TEST_ASSERT_EQUAL(1, calls);
}

TEST_CASE("hal switch: set_interrupt() can replace the callback",
          "[hal_switch]") {
    Switch b = make_switch();
    int first_calls = 0;
    int second_calls = 0;
    b.set_interrupt([&first_calls] { ++first_calls; });

    // Replace the handler: the close must reach the new callback only.
    b.set_interrupt([&second_calls] { ++second_calls; });
    b.set(SwitchState::kOn);
    TEST_ASSERT_EQUAL(1, second_calls);
    TEST_ASSERT_EQUAL(0, first_calls);
}

TEST_CASE("hal switch: two switches keep independent handlers",
          "[hal_switch]") {
    Switch a = make_switch(0);
    Switch b = make_switch(1);
    int a_calls = 0;
    int b_calls = 0;
    a.set_interrupt([&a_calls] { ++a_calls; });
    b.set_interrupt([&b_calls] { ++b_calls; });

    a.set(SwitchState::kOn);
    TEST_ASSERT_EQUAL(1, a_calls);
    TEST_ASSERT_EQUAL(0, b_calls);

    b.set(SwitchState::kOn);
    TEST_ASSERT_EQUAL(1, a_calls);
    TEST_ASSERT_EQUAL(1, b_calls);

    // Clearing one handler leaves the other in place.
    a.set_interrupt({});
    a.set(SwitchState::kOff);
    b.set(SwitchState::kOff);
    TEST_ASSERT_EQUAL(1, a_calls);
    TEST_ASSERT_EQUAL(2, b_calls);
}

// --- The stub satisfies the shared concept ---------------------------------

TEST_CASE("hal switch: stub Switch models SwitchLike", "[hal_switch]") {
    static_assert(thermo::hal::SwitchLike<Switch>);
    // create() must return the same concrete type (part of the concept).
    static_assert(std::same_as<decltype(Switch::create(1)), Switch>);
    TEST_ASSERT_EQUAL(ESP_OK, make_switch().init());
}
