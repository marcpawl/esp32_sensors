// Tests for the stub aggregate Board (`hal_stub`).
//
// The Board is a convenience aggregate: it owns one stub device per HAL device
// (chip temperature, 1-Wire bus, green/red LEDs, push button, mode switch) and
// exposes them. These tests cover the *aggregate* contract — construction from
// `BoardPins`, `init_all()`, that each accessor returns a live, usable device,
// and that the underlying stub controls (`set_sensors()`, `set()`,
// `set_pins()`, `set_celsius()`, `write()`) remain reachable through the board.
// The devices' own behaviour is covered by their dedicated tests
// (test_hal_switch.cpp, test_hal_led.cpp, test_hal_push_button.cpp,
// test_hal_onewire.cpp, test_hal_chip_temperature.cpp); here we only verify the
// wiring.
//
// They run on the ESP-IDF `linux` target with Unity and link the pin-free
// `hal_stub` component (never `hal_board`). Every device is deterministic, so
// no hardware is needed.
#include "unity.h"

#include <concepts>
#include <vector>

#include "hal/board.hpp"
#include "hal/board_types.hpp"
#include "hal/chip_temperature.hpp"
#include "hal/led.hpp"
#include "hal/network_time_protocol.hpp"
#include "hal/onewire.hpp"
#include "hal/pin_map.hpp"
#include "hal/push_button.hpp"
#include "hal/switch.hpp"

namespace {

// Convenience aliases for the shared HAL vocabulary.
using thermo::hal::Board;
using thermo::hal::BoardPins;
using thermo::hal::ButtonState;
using thermo::hal::LedState;
using thermo::hal::Pins;
using thermo::hal::PinState;

// Pin numbers are irrelevant to the stub, but every device takes them for
// interface parity with the GPIO-backed implementation.
constexpr BoardPins kPins{
    .one_wire = 4,
    .green_led = 17,
    .red_led = 16,
    .green_name = "Green",
    .red_name = "Red",
    .button = 0,
    .switch_first = 1,
    .switch_third = 2,
};

Board make_board() {
    Board b = Board::create(kPins);
    b.init_all();
    return b;
}

}  // namespace

// --- Construction / factory -------------------------------------------------

TEST_CASE("hal board: BoardPins defaults to the board pin map (§2.4)",
          "[hal_board]") {
    // The default wiring comes from `hal/pin_map.hpp`, so a board can be built
    // on the reference hardware without repeating the numbers.
    const BoardPins def{};
    TEST_ASSERT_EQUAL(thermo::pins::kWireBus, def.one_wire);
    TEST_ASSERT_EQUAL(thermo::pins::kGreenLed, def.green_led);
    TEST_ASSERT_EQUAL(thermo::pins::kRedLed, def.red_led);
    TEST_ASSERT_EQUAL(thermo::pins::kConfig, def.switch_first);
    TEST_ASSERT_EQUAL(thermo::pins::kRun, def.switch_third);

    // A default-constructed board still initializes and works (the stub
    // ignores the pins, but this proves `create({})` is usable).
    Board b = Board::create({});
    TEST_ASSERT_EQUAL(ESP_OK, b.init_all());
}

TEST_CASE("hal board: create() returns a usable, inited board", "[hal_board]") {
    Board b = Board::create(kPins);
    // init_all() always succeeds for the stub (every device init is a no-op).
    TEST_ASSERT_EQUAL(ESP_OK, b.init_all());
    // Each accessor returns a live device.
    TEST_ASSERT_EQUAL(static_cast<int>(ButtonState::kOff),
                      static_cast<int>(b.button().read()));
}

TEST_CASE("hal board: init_all() is idempotent and returns ESP_OK",
          "[hal_board]") {
    Board b = make_board();
    TEST_ASSERT_EQUAL(ESP_OK, b.init_all());
    TEST_ASSERT_EQUAL(ESP_OK, b.init_all());
}

// --- Each accessor wires up an independent, usable device -------------------

TEST_CASE("hal board: chip_temperature() is a live stub device",
          "[hal_board]") {
    Board b = make_board();
    TEST_ASSERT_EQUAL(2500, b.chip_temperature().read());  // default 25.00 °C
    b.chip_temperature().set_celsius(3150);
    TEST_ASSERT_EQUAL(3150, b.chip_temperature().read());
}

TEST_CASE("hal board: one_wire() is a live stub device", "[hal_board]") {
    Board b = make_board();
    // The stub starts with the three example sensors from §5.1.3.
    TEST_ASSERT_EQUAL(3, b.one_wire().read().size());

    const std::vector<thermo::hal::DS18B20Reading> readings = {
        {{0x28, 0xFF, 0x64, 0xA1, 0xB2, 0x16, 0x03, 0xC4}, 2150},
    };
    b.one_wire().set_sensors(readings);
    TEST_ASSERT_EQUAL(1, b.one_wire().discover_sensors().size());
    TEST_ASSERT_EQUAL(1, b.one_wire().read().size());
}

TEST_CASE("hal board: green() and red() are distinct, live stub devices",
          "[hal_board]") {
    Board b = make_board();
    TEST_ASSERT_EQUAL(static_cast<int>(LedState::kOff),
                      static_cast<int>(b.green().state()));
    TEST_ASSERT_EQUAL(static_cast<int>(LedState::kOff),
                      static_cast<int>(b.red().state()));

    b.green().write(LedState::kOn);
    // Writing the green LED must not affect the red one.
    TEST_ASSERT_EQUAL(static_cast<int>(LedState::kOn),
                      static_cast<int>(b.green().state()));
    TEST_ASSERT_EQUAL(static_cast<int>(LedState::kOff),
                      static_cast<int>(b.red().state()));

    b.red().write(LedState::kOn);
    b.green().write(LedState::kOff);
    TEST_ASSERT_EQUAL(static_cast<int>(LedState::kOn),
                      static_cast<int>(b.red().state()));
    TEST_ASSERT_EQUAL(static_cast<int>(LedState::kOff),
                      static_cast<int>(b.green().state()));
}

TEST_CASE("hal board: button() is a live stub device", "[hal_board]") {
    Board b = make_board();
    TEST_ASSERT_EQUAL(static_cast<int>(ButtonState::kOff),
                      static_cast<int>(b.button().read()));
    b.button().set(ButtonState::kOn);
    TEST_ASSERT_EQUAL(static_cast<int>(ButtonState::kOn),
                      static_cast<int>(b.button().read()));
}

TEST_CASE("hal board: mode_switch() is a live stub device", "[hal_board]") {
    Board b = make_board();
    // Default reading is the Third position (first off, third on).
    const Pins def = b.mode_switch().read();
    TEST_ASSERT_FALSE(def.first_on());
    TEST_ASSERT_TRUE(def.third_on());

    const Pins first{PinState::kOn, PinState::kOff};
    b.mode_switch().set_pins(first);
    TEST_ASSERT_TRUE(b.mode_switch().read() == first);
}

// --- The board exposes the const accessors too ------------------------------

TEST_CASE("hal board: const accessors yield const device references",
          "[hal_board]") {
    const Board b = make_board();
    // Reading through a const board is well-formed (proves the const overloads
    // exist and return the const-qualified device types).
    TEST_ASSERT_EQUAL(2500, b.chip_temperature().read());
    TEST_ASSERT_EQUAL(static_cast<int>(ButtonState::kOff),
                      static_cast<int>(b.button().read()));
    // The 1-Wire stub defaults to the three example sensors from §5.1.3.
    TEST_ASSERT_EQUAL(3, b.one_wire().read().size());
    TEST_ASSERT_EQUAL(static_cast<int>(LedState::kOff),
                      static_cast<int>(b.green().state()));
    TEST_ASSERT_EQUAL(static_cast<int>(LedState::kOff),
                      static_cast<int>(b.red().state()));
    const Pins p = b.mode_switch().read();
    TEST_ASSERT_FALSE(p.first_on());
    // The const accessor yields a const NTP reference (the const overload
    // exists); the query itself is non-const and is covered above.
    static_assert(std::same_as<decltype(b.network_time_protocol()),
                               thermo::hal::NetworkTimeProtocol const&>);
}

// --- The stub satisfies the shared concept ---------------------------------

TEST_CASE("hal board: stub Board models BoardLike", "[hal_board]") {
    static_assert(thermo::hal::BoardLike<Board>);
    // create() must return the same concrete type (part of the concept).
    static_assert(std::same_as<decltype(Board::create(kPins)), Board>);
    TEST_ASSERT_EQUAL(ESP_OK, make_board().init_all());
}
