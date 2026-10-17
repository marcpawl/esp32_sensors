// Native link check for the 1-Wire HAL (`hal_stub`).
//
// Proves the stub 1-Wire bus links and works under real FreeRTOS on the linux
// target: create/init, `set_sensors()`, and one `discover_sensors()` / `read()`
// round-trip. The host suite in `test/` covers the stub in depth.
#include "unity.h"

#include <vector>

#include "hal/onewire.hpp"
#include "hal/onewire_types.hpp"

namespace {
using thermo::hal::DS18B20Reading;
using thermo::hal::OneWire;
using thermo::hal::RomId;

const RomId kSensor{0x28, 0xFF, 0x64, 0xA1, 0xB2, 0x16, 0x03, 0xC4};
}  // namespace

TEST_CASE("espidf_cpp_test onewire: create/init and round-trips a sensor",
          "[espidf_cpp_test][onewire]") {
    static_assert(thermo::hal::OneWireLike<OneWire>);

    OneWire bus = OneWire::create(/*gpio=*/4);
    TEST_ASSERT_EQUAL(ESP_OK, bus.init());

    bus.set_sensors(std::vector<DS18B20Reading>{{kSensor, 2150}});

    const std::vector<RomId> found = bus.discover_sensors();
    TEST_ASSERT_EQUAL_UINT(1, static_cast<unsigned>(found.size()));
    TEST_ASSERT_TRUE(found[0] == kSensor);

    const std::vector<DS18B20Reading> readings = bus.read();
    TEST_ASSERT_EQUAL_UINT(1, static_cast<unsigned>(readings.size()));
    TEST_ASSERT_TRUE(readings[0].temp_c_x100.has_value());
    TEST_ASSERT_EQUAL_INT(2150, readings[0].temp_c_x100.value());
}
