// Tests for the stub switch (`hal_stub`).
//
// The switch HAL is deliberately semantics-free: `Switch::read()` returns
// whether the switch is currently closed as a `SwitchState`, and interpreting
// a transition (edge detection, debouncing, hold timing) is the caller's job.
// These tests therefore cover the *stub device* contract — construction,
// `init()`, the default reading, round-trips through `set()`, the interrupt
// callback registered with `set_interrupt()`, and the shared `SwitchLike`
// concept.
// They run on the ESP-IDF `linux` target with Unity and link the pin-free
// `hal_stub` component (never `hal_board`).
//
// The stub holds its reported state in software, so every case is
// deterministic and needs no hardware. The stub has no real interrupt, so
// `set_interrupt()` callbacks are invoked synchronously from `set()`.
// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#pragma GCC diagnostic pop
// NOLINTEND

#include <concepts>
#include <optional>

#include "hal/switch_types.hpp"
#include "hal_stub/switch.hpp"
#include "unity.h"

namespace {

// Convenience aliases for the shared HAL vocabulary.
using thermo::hal::SwitchState;
using thermo::hal::stub::Switch;

// Pin numbers are irrelevant to the stub, but the factory takes them for
// interface parity with the GPIO-backed implementation.
constexpr int kSwitchGpio = 0;

Switch make_switch() {
    Switch b = Switch::create(kSwitchGpio);
    b.init();
    return b;
}

// Records the calls a registered callback receives, so tests can assert on the
// sequence of state changes.
struct CallbackLog {
    int calls = 0;
    SwitchState last = SwitchState::kOff;
    // Number of closes / opens observed, useful for edge assertions.
    int closes = 0;
    int opens = 0;

    void record(SwitchState state) noexcept {
        ++calls;
        last = state;
        if (state == SwitchState::kOn) {
            ++closes;
        } else {
            ++opens;
        }
    }
};

// File-scope log the free-function callback below writes to. Only one test at
// a time uses it, so a single instance is sufficient.
CallbackLog g_log;

void record_callback(SwitchState state) noexcept {
    g_log.record(state);
}

// A second callback and log, so the replacement test can prove that events
// reach the new handler and not the old one.
CallbackLog g_other_log;

void other_callback(SwitchState state) noexcept {
    g_other_log.record(state);
}

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
    g_log = {};
    b.set(SwitchState::kOn);
    b.set(SwitchState::kOff);
    TEST_ASSERT_EQUAL(0, g_log.calls);
}

TEST_CASE("hal switch: set_interrupt() fires on a state change",
          "[hal_switch]") {
    Switch b = make_switch();
    g_log = {};
    b.set_interrupt(record_callback);

    b.set(SwitchState::kOn);
    TEST_ASSERT_EQUAL(1, g_log.calls);
    TEST_ASSERT_EQUAL(static_cast<int>(SwitchState::kOn),
                      static_cast<int>(g_log.last));

    b.set(SwitchState::kOff);
    TEST_ASSERT_EQUAL(2, g_log.calls);
    TEST_ASSERT_EQUAL(static_cast<int>(SwitchState::kOff),
                      static_cast<int>(g_log.last));
}

TEST_CASE("hal switch: callback is not fired without a state change",
          "[hal_switch]") {
    Switch b = make_switch();
    g_log = {};
    b.set_interrupt(record_callback);

    b.set(SwitchState::kOn);
    b.set(SwitchState::kOn);  // unchanged: no second notification
    TEST_ASSERT_EQUAL(1, g_log.calls);

    b.set(SwitchState::kOff);
    b.set(SwitchState::kOff);  // unchanged: no second notification
    TEST_ASSERT_EQUAL(2, g_log.calls);
}

TEST_CASE("hal switch: callback reports the close/open sequence",
          "[hal_switch]") {
    Switch b = make_switch();
    g_log = {};
    b.set_interrupt(record_callback);

    b.set(SwitchState::kOn);
    b.set(SwitchState::kOff);
    b.set(SwitchState::kOn);

    TEST_ASSERT_EQUAL(3, g_log.calls);
    TEST_ASSERT_EQUAL(2, g_log.closes);
    TEST_ASSERT_EQUAL(1, g_log.opens);
    TEST_ASSERT_EQUAL(static_cast<int>(SwitchState::kOn),
                      static_cast<int>(g_log.last));
}

TEST_CASE("hal switch: set_interrupt(nullopt) unregisters the callback",
          "[hal_switch]") {
    Switch b = make_switch();
    g_log = {};
    b.set_interrupt(record_callback);
    b.set(SwitchState::kOn);
    TEST_ASSERT_EQUAL(1, g_log.calls);

    // Clear the callback: subsequent changes are silent.
    b.set_interrupt(std::nullopt);
    b.set(SwitchState::kOff);
    b.set(SwitchState::kOn);
    TEST_ASSERT_EQUAL(1, g_log.calls);
}

TEST_CASE("hal switch: set_interrupt() can replace the callback",
          "[hal_switch]") {
    Switch b = make_switch();
    g_log = {};
    g_other_log = {};
    b.set_interrupt(record_callback);

    // Replace the handler: the close must reach the new callback only.
    b.set_interrupt(other_callback);
    b.set(SwitchState::kOn);
    TEST_ASSERT_EQUAL(1, g_other_log.calls);
    TEST_ASSERT_EQUAL(0, g_log.calls);
}

// --- The stub satisfies the shared concept ---------------------------------

TEST_CASE("hal switch: stub Switch models SwitchLike", "[hal_switch]") {
    static_assert(thermo::hal::SwitchLike<Switch>);
    // create() must return the same concrete type (part of the concept).
    static_assert(std::same_as<decltype(Switch::create(1)), Switch>);
    TEST_ASSERT_EQUAL(ESP_OK, make_switch().init());
}
