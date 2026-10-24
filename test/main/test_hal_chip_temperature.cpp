// Tests for the stub chip-temperature sensor (`hal_stub`, §2.1, §10).
//
// The chip-temperature HAL is deliberately minimal: the no-argument `create()`
// factory, `init()`, `read()` (hundredths of a degree Celsius), and the
// stub-only `set_celsius()` used to control the reported value. These tests
// cover the *stub device* contract — construction, `init()`, the default
// reading, round-trips through `set_celsius()`, and the shared
// `ChipTemperatureLike` concept. They run on the ESP-IDF `linux` target with
// Unity and link the pin-free `hal_stub` component (never `hal_board`).
//
// The stub holds its reading in software, so every case is deterministic and
// needs no hardware.
// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#pragma GCC diagnostic pop
// NOLINTEND

#include <concepts>

#include "hal/chip_temperature.hpp"
#include "hal/chip_temperature_types.hpp"
#include "unity.h"

namespace {

// Convenience alias for the shared HAL vocabulary.
using thermo::hal::ChipTemperature;

ChipTemperature make_sensor() {
    ChipTemperature sensor = ChipTemperature::create();
    sensor.init();
    return sensor;
}

}  // namespace

// --- Construction / factory -------------------------------------------------

TEST_CASE("hal chip temperature: create() takes no arguments",
          "[hal_chip_temperature]") {
    // The factory is parameterless for the stub and the board alike.
    static_assert(
        std::same_as<decltype(ChipTemperature::create()), ChipTemperature>);
    ChipTemperature sensor = ChipTemperature::create();
    TEST_ASSERT_EQUAL(ESP_OK, sensor.init());
}

TEST_CASE("hal chip temperature: init() is idempotent and returns ESP_OK",
          "[hal_chip_temperature]") {
    ChipTemperature sensor = make_sensor();
    TEST_ASSERT_EQUAL(ESP_OK, sensor.init());
    TEST_ASSERT_EQUAL(ESP_OK, sensor.init());
}

// --- Default reading --------------------------------------------------------

TEST_CASE("hal chip temperature: defaults to a plausible room temperature",
          "[hal_chip_temperature]") {
    const ChipTemperature sensor = make_sensor();
    TEST_ASSERT_EQUAL_INT16(2500, sensor.read());
}

// --- Round-trips through set_celsius() --------------------------------------

TEST_CASE("hal chip temperature: set_celsius() is reflected by read()",
          "[hal_chip_temperature]") {
    ChipTemperature sensor = make_sensor();

    sensor.set_celsius(2350);
    TEST_ASSERT_EQUAL_INT16(2350, sensor.read());

    sensor.set_celsius(-500);
    TEST_ASSERT_EQUAL_INT16(-500, sensor.read());
}

TEST_CASE("hal chip temperature: repeated set_celsius() keeps the last value",
          "[hal_chip_temperature]") {
    ChipTemperature sensor = make_sensor();
    sensor.set_celsius(1000);
    sensor.set_celsius(4000);
    sensor.set_celsius(3050);  // last write wins
    TEST_ASSERT_EQUAL_INT16(3050, sensor.read());
}

TEST_CASE("hal chip temperature: set_celsius(0) is a valid reading",
          "[hal_chip_temperature]") {
    ChipTemperature sensor = make_sensor();
    sensor.set_celsius(0);
    TEST_ASSERT_EQUAL_INT16(0, sensor.read());
}

// --- The stub satisfies the shared concept ---------------------------------

TEST_CASE("hal chip temperature: stub ChipTemperature models the concept",
          "[hal_chip_temperature]") {
    static_assert(thermo::hal::ChipTemperatureLike<ChipTemperature>);
    TEST_ASSERT_EQUAL(ESP_OK, make_sensor().init());
}
