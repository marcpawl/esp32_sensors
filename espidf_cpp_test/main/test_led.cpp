// Native link check for the LED HAL (`hal_stub`).
//
// Proves the stub LED links and works under real FreeRTOS on the linux target.
// Behaviour is a single write/read round-trip; the host suite in `test/` covers
// the stub's logging and the `LedLike` concept in depth.
#include "unity.h"

#include "hal/led.hpp"
#include "hal/led_types.hpp"

namespace {
using thermo::hal::Led;
using thermo::hal::LedState;
}  // namespace

TEST_CASE("espidf_cpp_test led: create/init and round-trips a write",
          "[espidf_cpp_test][led]") {
    static_assert(thermo::hal::LedLike<Led>);

    Led led = Led::create(/*gpio=*/17, /*name=*/"Green");
    TEST_ASSERT_EQUAL(ESP_OK, led.init());

    led.write(LedState::kOn);
    TEST_ASSERT_EQUAL(static_cast<int>(LedState::kOn),
                      static_cast<int>(led.state()));

    led.write(LedState::kOff);
    TEST_ASSERT_EQUAL(static_cast<int>(LedState::kOff),
                      static_cast<int>(led.state()));
}
