// Tests for the stub 1-Wire bus (`hal_stub`, §2.1, §2.4).
//
// The bus HAL is deliberately minimal: `discover_sensors()` returns the ROM IDs
// of the DS18B20 devices present on the wire and `read()` returns one
// `DS18B20Reading` (ROM ID + temperature) per device. These tests cover the
// *stub device* contract — construction, `init()`, the default collection,
// control via `set_sensors()`, `read()`, and the shared `OneWireLike` concept.
// They run on the ESP-IDF `linux` target with Unity and link the pin-free
// `hal_stub` component (never `hal_board`).
//
// The stub holds its sensor collection in software, so every case is
// deterministic and needs no hardware.
#include "unity.h"

#include <concepts>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "hal/ds18b20_types.hpp"
#include "hal/onewire.hpp"
#include "hal/onewire_types.hpp"

namespace {

// Convenience aliases for the shared HAL vocabulary.
using thermo::hal::DS18B20Reading;
using thermo::hal::OneWire;
using thermo::hal::RomId;

// Pin number is irrelevant to the stub, but the factory takes it for interface
// parity with the GPIO-backed implementation.
constexpr int kDataGpio = 4;

RomId make_rom(std::initializer_list<unsigned> bytes) {
    RomId id{};
    std::size_t i = 0;
    for (unsigned b : bytes) {
        if (i < id.bytes.size()) {
            id.bytes[i++] = static_cast<std::uint8_t>(b);
        }
    }
    return id;
}

// Builds a reading from a ROM ID and an optional temperature (x100 °C).
DS18B20Reading make_reading(std::initializer_list<unsigned> bytes,
                            std::optional<std::int16_t> temp = std::nullopt) {
    return DS18B20Reading{make_rom(bytes), temp};
}

OneWire make_bus() {
    OneWire bus = OneWire::create(kDataGpio);
    bus.init();
    return bus;
}

}  // namespace

// --- Construction / factory -------------------------------------------------

TEST_CASE("hal onewire: create() returns a usable, inited bus",
          "[hal_onewire]") {
    OneWire bus = OneWire::create(kDataGpio);
    // init() always succeeds for the stub (there is no pin to configure).
    TEST_ASSERT_EQUAL(ESP_OK, bus.init());
    // discover_sensors() is callable and yields the default collection.
    TEST_ASSERT_EQUAL_UINT(3, bus.discover_sensors().size());
}

TEST_CASE("hal onewire: init() is idempotent and returns ESP_OK",
          "[hal_onewire]") {
    OneWire bus = make_bus();
    TEST_ASSERT_EQUAL(ESP_OK, bus.init());
    TEST_ASSERT_EQUAL(ESP_OK, bus.init());
}

// --- Default collection -----------------------------------------------------

TEST_CASE("hal onewire: defaults to the three spec sensors", "[hal_onewire]") {
    const OneWire bus = make_bus();
    const std::vector<RomId> sensors = bus.discover_sensors();

    TEST_ASSERT_EQUAL_UINT(3, sensors.size());
    // §5.1.3 example ROM IDs, in order.
    TEST_ASSERT_TRUE(sensors[0].bytes[7] == 0xC4);
    TEST_ASSERT_TRUE(sensors[1].bytes[7] == 0x2F);
    TEST_ASSERT_TRUE(sensors[2].bytes[7] == 0xD1);
    for (const RomId& rom : sensors) {
        TEST_ASSERT_EQUAL_UINT8(0x28, rom.bytes[0]);  // DS18B20 family code.
    }
}

TEST_CASE("hal onewire: the default readings match the discovery order",
          "[hal_onewire]") {
    const OneWire bus = make_bus();
    const std::vector<DS18B20Reading> readings = bus.read();
    const std::vector<RomId> roms = bus.discover_sensors();

    TEST_ASSERT_EQUAL_UINT(roms.size(), readings.size());
    for (std::size_t i = 0; i < readings.size(); ++i) {
        TEST_ASSERT_TRUE(readings[i].rom == roms[i]);
    }
}

// --- Round-trips through set_sensors() --------------------------------------

TEST_CASE("hal onewire: set_sensors() is reflected by discover_sensors()",
          "[hal_onewire]") {
    OneWire bus = make_bus();

    const std::vector<DS18B20Reading> two = {
        make_reading({0x28, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07}),
        make_reading({0x28, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17}),
    };
    bus.set_sensors(two);
    const std::vector<RomId> got = bus.discover_sensors();
    TEST_ASSERT_EQUAL_UINT(2, got.size());
    TEST_ASSERT_TRUE(got[0] == two[0].rom);
    TEST_ASSERT_TRUE(got[1] == two[1].rom);
}

TEST_CASE("hal onewire: set_sensors({}) yields an empty collection",
          "[hal_onewire]") {
    OneWire bus = make_bus();
    bus.set_sensors({});
    TEST_ASSERT_EQUAL_UINT(0, bus.discover_sensors().size());
    TEST_ASSERT_EQUAL_UINT(0, bus.read().size());
}

TEST_CASE("hal onewire: repeated set_sensors() keeps the last value",
          "[hal_onewire]") {
    OneWire bus = make_bus();

    const std::vector<DS18B20Reading> first = {
        make_reading({0x28, 1, 2, 3, 4, 5, 6, 7})};
    const std::vector<DS18B20Reading> last = {
        make_reading({0x28, 9, 8, 7, 6, 5, 4, 3})};

    bus.set_sensors(first);
    bus.set_sensors(last);  // last write wins
    const std::vector<RomId> got = bus.discover_sensors();
    TEST_ASSERT_EQUAL_UINT(1, got.size());
    TEST_ASSERT_TRUE(got[0] == last[0].rom);
}

TEST_CASE("hal onewire: discover_sensors() returns a copy", "[hal_onewire]") {
    OneWire bus = make_bus();
    const std::vector<DS18B20Reading> first = {
        make_reading({0x28, 1, 2, 3, 4, 5, 6, 7})};
    bus.set_sensors(first);

    std::vector<RomId> snapshot = bus.discover_sensors();
    // Mutating the returned vector must not affect the bus.
    snapshot.clear();
    TEST_ASSERT_EQUAL_UINT(1, bus.discover_sensors().size());
}

// --- read() returns readings ------------------------------------------------

TEST_CASE("hal onewire: read() returns the set readings in order",
          "[hal_onewire]") {
    OneWire bus = make_bus();

    const std::vector<DS18B20Reading> set = {
        make_reading({0x28, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07}, 1234),
        make_reading({0x28, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17}, -500),
    };
    bus.set_sensors(set);

    const std::vector<DS18B20Reading> got = bus.read();
    TEST_ASSERT_EQUAL_UINT(2, got.size());
    for (std::size_t i = 0; i < set.size(); ++i) {
        TEST_ASSERT_TRUE(got[i].rom == set[i].rom);
        TEST_ASSERT_TRUE(got[i].temp_c_x100 == set[i].temp_c_x100);
    }
}

TEST_CASE("hal onewire: read() round-trips a missing temperature as nullopt",
          "[hal_onewire]") {
    OneWire bus = make_bus();
    bus.set_sensors({make_reading({0x28, 9, 9, 9, 9, 9, 9, 9}, std::nullopt)});

    const std::vector<DS18B20Reading> got = bus.read();
    TEST_ASSERT_EQUAL_UINT(1, got.size());
    TEST_ASSERT_FALSE(got[0].temp_c_x100.has_value());
}

TEST_CASE("hal onewire: read() returns a copy", "[hal_onewire]") {
    OneWire bus = make_bus();
    bus.set_sensors({make_reading({0x28, 1, 2, 3, 4, 5, 6, 7}, 2500)});

    std::vector<DS18B20Reading> snapshot = bus.read();
    snapshot.clear();
    TEST_ASSERT_EQUAL_UINT(1, bus.read().size());
}

// --- The stub satisfies the shared concept ---------------------------------

TEST_CASE("hal onewire: stub OneWire models OneWireLike", "[hal_onewire]") {
    static_assert(thermo::hal::OneWireLike<OneWire>);
    // create() must return the same concrete type (part of the concept).
    static_assert(std::same_as<decltype(OneWire::create(1)), OneWire>);
    TEST_ASSERT_EQUAL(ESP_OK, make_bus().init());
}
