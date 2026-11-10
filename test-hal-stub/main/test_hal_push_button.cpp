// Tests for the stub push button (`hal_stub`).
//
// The button HAL is deliberately semantics-free: `PushButton::read()` returns
// whether the button is currently pressed as a `ButtonState`, and interpreting
// a press (edge detection, debouncing, hold timing) is the caller's job. These
// tests therefore cover the *stub device* contract — construction, `init()`,
// the default reading, round-trips through `set()`, the interrupt callback
// registered with `set_interrupt()`, and the shared `PushButtonLike` concept.
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

#include "hal/push_button.hpp"
#include "hal/push_button_types.hpp"
#include "unity.h"

namespace {

// Convenience aliases for the shared HAL vocabulary.
using thermo::hal::ButtonState;
using thermo::hal::PushButton;

// Pin numbers are irrelevant to the stub, but the factory takes them for
// interface parity with the GPIO-backed implementation.
constexpr int kButtonGpio = 0;

PushButton make_button() {
    PushButton b = PushButton::create(kButtonGpio);
    b.init();
    return b;
}

// Records the calls a registered callback receives, so tests can assert on the
// sequence of state changes.
struct CallbackLog {
    int calls = 0;
    ButtonState last = ButtonState::kOff;
    // Number of presses / releases observed, useful for edge assertions.
    int presses = 0;
    int releases = 0;

    void record(ButtonState state) noexcept {
        ++calls;
        last = state;
        if (state == ButtonState::kOn) {
            ++presses;
        } else {
            ++releases;
        }
    }
};

// File-scope log the free-function callback below writes to. Only one test at
// a time uses it, so a single instance is sufficient.
CallbackLog g_log;

void record_callback(ButtonState state) noexcept {
    g_log.record(state);
}

}  // namespace

// --- Construction / factory -------------------------------------------------

TEST_CASE("hal push button: create() returns a usable, inited button",
          "[hal_push_button]") {
    PushButton b = PushButton::create(kButtonGpio);
    // init() always succeeds for the stub (there is no pin to configure).
    TEST_ASSERT_EQUAL(ESP_OK, b.init());
    // A freshly created button reads as released.
    TEST_ASSERT_EQUAL(static_cast<int>(ButtonState::kOff),
                      static_cast<int>(b.read()));
}

TEST_CASE("hal push button: init() is idempotent and returns ESP_OK",
          "[hal_push_button]") {
    PushButton b = make_button();
    TEST_ASSERT_EQUAL(ESP_OK, b.init());
    TEST_ASSERT_EQUAL(ESP_OK, b.init());
}

// --- Default reading --------------------------------------------------------

TEST_CASE("hal push button: defaults to released", "[hal_push_button]") {
    const PushButton b = make_button();
    TEST_ASSERT_EQUAL(static_cast<int>(ButtonState::kOff),
                      static_cast<int>(b.read()));
}

// --- Round-trips through set() ---------------------------------------------

TEST_CASE("hal push button: set() is reflected by read()",
          "[hal_push_button]") {
    PushButton b = make_button();

    b.set(ButtonState::kOn);
    TEST_ASSERT_EQUAL(static_cast<int>(ButtonState::kOn),
                      static_cast<int>(b.read()));

    b.set(ButtonState::kOff);
    TEST_ASSERT_EQUAL(static_cast<int>(ButtonState::kOff),
                      static_cast<int>(b.read()));
}

TEST_CASE("hal push button: repeated set() keeps the last value",
          "[hal_push_button]") {
    PushButton b = make_button();
    b.set(ButtonState::kOn);
    b.set(ButtonState::kOff);
    b.set(ButtonState::kOn);  // last write wins
    TEST_ASSERT_EQUAL(static_cast<int>(ButtonState::kOn),
                      static_cast<int>(b.read()));
}

TEST_CASE("hal push button: setting the same state twice is stable",
          "[hal_push_button]") {
    PushButton b = make_button();
    b.set(ButtonState::kOn);
    b.set(ButtonState::kOn);
    TEST_ASSERT_EQUAL(static_cast<int>(ButtonState::kOn),
                      static_cast<int>(b.read()));
}

// --- Interrupt callback -----------------------------------------------------

TEST_CASE("hal push button: no callback by default", "[hal_push_button]") {
    // Without set_interrupt(), a state change calls nothing and must not crash.
    PushButton b = make_button();
    g_log = {};
    b.set(ButtonState::kOn);
    b.set(ButtonState::kOff);
    TEST_ASSERT_EQUAL(0, g_log.calls);
}

TEST_CASE("hal push button: set_interrupt() fires on a state change",
          "[hal_push_button]") {
    PushButton b = make_button();
    g_log = {};
    b.set_interrupt(record_callback);

    b.set(ButtonState::kOn);
    TEST_ASSERT_EQUAL(1, g_log.calls);
    TEST_ASSERT_EQUAL(static_cast<int>(ButtonState::kOn),
                      static_cast<int>(g_log.last));

    b.set(ButtonState::kOff);
    TEST_ASSERT_EQUAL(2, g_log.calls);
    TEST_ASSERT_EQUAL(static_cast<int>(ButtonState::kOff),
                      static_cast<int>(g_log.last));
}

TEST_CASE("hal push button: callback is not fired without a state change",
          "[hal_push_button]") {
    PushButton b = make_button();
    g_log = {};
    b.set_interrupt(record_callback);

    b.set(ButtonState::kOn);
    b.set(ButtonState::kOn);  // unchanged: no second notification
    TEST_ASSERT_EQUAL(1, g_log.calls);

    b.set(ButtonState::kOff);
    b.set(ButtonState::kOff);  // unchanged: no second notification
    TEST_ASSERT_EQUAL(2, g_log.calls);
}

TEST_CASE("hal push button: callback reports the press/release sequence",
          "[hal_push_button]") {
    PushButton b = make_button();
    g_log = {};
    b.set_interrupt(record_callback);

    b.set(ButtonState::kOn);
    b.set(ButtonState::kOff);
    b.set(ButtonState::kOn);

    TEST_ASSERT_EQUAL(3, g_log.calls);
    TEST_ASSERT_EQUAL(2, g_log.presses);
    TEST_ASSERT_EQUAL(1, g_log.releases);
    TEST_ASSERT_EQUAL(static_cast<int>(ButtonState::kOn),
                      static_cast<int>(g_log.last));
}

TEST_CASE("hal push button: set_interrupt(nullopt) unregisters the callback",
          "[hal_push_button]") {
    PushButton b = make_button();
    g_log = {};
    b.set_interrupt(record_callback);
    b.set(ButtonState::kOn);
    TEST_ASSERT_EQUAL(1, g_log.calls);

    // Clear the callback: subsequent changes are silent.
    b.set_interrupt(std::nullopt);
    b.set(ButtonState::kOff);
    b.set(ButtonState::kOn);
    TEST_ASSERT_EQUAL(1, g_log.calls);
}

TEST_CASE("hal push button: set_interrupt() can replace the callback",
          "[hal_push_button]") {
    PushButton b = make_button();
    g_log = {};
    b.set_interrupt(record_callback);

    CallbackLog other;
    // A capture-less lambda converts to the function pointer the handler holds;
    // it writes into a different log to prove the handler was replaced.
    b.set_interrupt([](ButtonState state) noexcept { g_log.record(state); });
    b.set(ButtonState::kOn);
    TEST_ASSERT_EQUAL(1, g_log.calls);
    TEST_ASSERT_EQUAL(0, other.calls);
}

// --- The stub satisfies the shared concept ---------------------------------

TEST_CASE("hal push button: stub PushButton models PushButtonLike",
          "[hal_push_button]") {
    static_assert(thermo::hal::PushButtonLike<PushButton>);
    // create() must return the same concrete type (part of the concept).
    static_assert(std::same_as<decltype(PushButton::create(1)), PushButton>);
    TEST_ASSERT_EQUAL(ESP_OK, make_button().init());
}
