// Native link check for the chip-temperature HAL (`hal_stub`).
//
// Proves the stub chip-temperature sensor links and works under real FreeRTOS
// on the linux target: create/init and one `set_celsius()` / `read()`
// round-trip. The host suite in `test/` covers the stub in depth.
#include "unity.h"

#include "hal/chip_temperature.hpp"
#include "hal/chip_temperature_types.hpp"

namespace {
using thermo::hal::ChipTemperature;
}  // namespace

TEST_CASE("qemu chip temperature: create/init and round-trips a reading",
          "[qemu][chip_temperature]") {
    static_assert(thermo::hal::ChipTemperatureLike<ChipTemperature>);

    ChipTemperature sensor = ChipTemperature::create();
    TEST_ASSERT_EQUAL(ESP_OK, sensor.init());

    // Has a plausible default reading, then honours a set value.
    TEST_ASSERT_TRUE(sensor.read() > 0);

    sensor.set_celsius(3150);
    TEST_ASSERT_EQUAL_INT(3150, sensor.read());
}
