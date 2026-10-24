// Tests for the stub 3-position mode switch (`hal_stub`, §2.3).
//
// The HAL is deliberately semantics-free: `Switch::read()` returns the raw
// state of the two sense poles as a `Pins`, and mapping that to Configure /
// Off / Run is the caller's job. These tests therefore cover the *stub
// device* contract — construction, `init()`, the default reading, round-trips
// through `set_pins()`, and the ability to simulate bad hardware (both poles on
// or both poles off). They run on the ESP-IDF `linux` target with Unity and
// link the pin-free `hal_stub` component (never `hal_board`).
//
// The stub holds its reported state in software, so every case is
// deterministic and needs no hardware.
// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#pragma GCC diagnostic pop
// NOLINTEND

#include <concepts>

#include "hal/switch.hpp"
#include "hal/switch_types.hpp"
#include "unity.h"

// Unity's TEST_ASSERT_EQUAL casts its operands to UNITY_INT, so it cannot
// compare a `Pins` value directly. Pins already provides `operator==`; wrap it
// in a bool assertion with a readable message showing the raw pole states.
#define ASSERT_PINS_EQUAL(expected, actual)                    \
    do {                                                       \
        const thermo::hal::Pins assert_pins_exp = (expected);  \
        const thermo::hal::Pins assert_pins_act = (actual);    \
        TEST_ASSERT_TRUE_MESSAGE(                              \
            assert_pins_exp == assert_pins_act,                \
            "Pins mismatch (first/third): expected " #expected \
            " got a different reading");                       \
    } while (0)

namespace {

// Convenience aliases for the shared HAL vocabulary.
using thermo::hal::Pins;
using thermo::hal::PinState;
using thermo::hal::Switch;

// The three named, healthy positions expressed as raw pole readings. These
// mirror the truth table documented in hal/switch_types.hpp.
constexpr Pins kFirst{PinState::kOn, PinState::kOff};
constexpr Pins kCenter{PinState::kOff, PinState::kOff};
constexpr Pins kThird{PinState::kOff, PinState::kOn};

// Pin numbers are irrelevant to the stub, but the factory takes them for
// interface parity with the GPIO-backed implementation.
constexpr int kFirstGpio = 4;
constexpr int kThirdGpio = 5;

Switch make_switch() {
    Switch s = Switch::create(kFirstGpio, kThirdGpio);
    s.init();
    return s;
}

}  // namespace

// --- Construction / factory -------------------------------------------------

TEST_CASE("hal switch: create() returns a usable, inited switch",
          "[hal_switch]") {
    Switch s = Switch::create(kFirstGpio, kThirdGpio);
    // init() always succeeds for the stub (there are no pins to configure).
    TEST_ASSERT_EQUAL(ESP_OK, s.init());
    // read() is callable and yields one of the two sane states.
    const Pins p = s.read();
    TEST_ASSERT_TRUE(p == kFirst || p == kCenter || p == kThird);
}

TEST_CASE("hal switch: init() is idempotent and returns ESP_OK",
          "[hal_switch]") {
    Switch s = make_switch();
    TEST_ASSERT_EQUAL(ESP_OK, s.init());
    TEST_ASSERT_EQUAL(ESP_OK, s.init());
}

// --- Default reading --------------------------------------------------------

TEST_CASE("hal switch: defaults to the Third position", "[hal_switch]") {
    // Documented default: first off, third on.
    const Switch s = make_switch();
    const Pins p = s.read();
    ASSERT_PINS_EQUAL(kThird, p);
    TEST_ASSERT_FALSE(p.first_on());
    TEST_ASSERT_TRUE(p.third_on());
}

// --- Pins is a usable value type -------------------------------------------

TEST_CASE("hal switch: Pins equality and accessors", "[hal_switch]") {
    TEST_ASSERT_TRUE(kFirst == Pins(PinState::kOn, PinState::kOff));
    TEST_ASSERT_FALSE(kFirst == kThird);
    TEST_ASSERT_TRUE(Pins().first_on() == false);
    TEST_ASSERT_TRUE(Pins().third_on() == false);
    TEST_ASSERT_TRUE(kThird.third_on());
    TEST_ASSERT_FALSE(kThird.first_on());
}

// --- Round-trips through set_pins() ----------------------------------------

TEST_CASE("hal switch: set_pins() is reflected by read()", "[hal_switch]") {
    Switch s = make_switch();

    s.set_pins(kFirst);
    ASSERT_PINS_EQUAL(kFirst, s.read());

    s.set_pins(kCenter);
    ASSERT_PINS_EQUAL(kCenter, s.read());

    s.set_pins(kThird);
    ASSERT_PINS_EQUAL(kThird, s.read());
}

TEST_CASE("hal switch: repeated set_pins() keeps the last value",
          "[hal_switch]") {
    Switch s = make_switch();
    s.set_pins(kFirst);
    s.set_pins(kCenter);
    s.set_pins(kFirst);  // last write wins
    ASSERT_PINS_EQUAL(kFirst, s.read());
}

// --- Simulating bad hardware ------------------------------------------------

TEST_CASE("hal switch: both poles on is observable", "[hal_switch]") {
    // A healthy ON-OFF-ON switch energizes at most one pole, so both-on is a
    // fault the caller can detect (and the stub can reproduce).
    Switch s = make_switch();
    const Pins both_on{PinState::kOn, PinState::kOn};
    s.set_pins(both_on);
    const Pins p = s.read();
    TEST_ASSERT_TRUE(p.first_on());
    TEST_ASSERT_TRUE(p.third_on());
    ASSERT_PINS_EQUAL(both_on, p);
}

TEST_CASE("hal switch: both poles off is observable", "[hal_switch]") {
    Switch s = make_switch();
    const Pins both_off{PinState::kOff, PinState::kOff};
    s.set_pins(both_off);
    const Pins p = s.read();
    TEST_ASSERT_FALSE(p.first_on());
    TEST_ASSERT_FALSE(p.third_on());
    ASSERT_PINS_EQUAL(both_off, p);
}

// --- The stub satisfies the shared concept ---------------------------------

TEST_CASE("hal switch: stub Switch models SwitchLike", "[hal_switch]") {
    static_assert(thermo::hal::SwitchLike<Switch>);
    // create() must return the same concrete type (part of the concept).
    static_assert(std::same_as<decltype(Switch::create(1, 2)), Switch>);
    TEST_ASSERT_EQUAL(ESP_OK, make_switch().init());
}
