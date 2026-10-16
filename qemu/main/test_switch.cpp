// QEMU link check for the mode-switch HAL (`hal_stub`).
//
// The QEMU project links the pin-free `hal_stub` HAL, not the GPIO-backed
// `hal_board`. On the ESP32-S3 target this proves the stub switch links and
// works under real FreeRTOS; the deeper behaviour (every pin combination,
// bad-hardware cases) is covered by the host suite in `test/`.
#include "unity.h"

#include "hal/switch.hpp"
#include "hal/switch_types.hpp"

namespace {
using thermo::hal::PinState;
using thermo::hal::Switch;
}  // namespace

TEST_CASE("qemu switch: create/init and round-trips a pin state",
          "[qemu][switch]") {
    static_assert(thermo::hal::SwitchLike<Switch>);

    Switch sw = Switch::create(/*first=*/1, /*third=*/2);
    TEST_ASSERT_EQUAL(ESP_OK, sw.init());

    const thermo::hal::Pins center{PinState::kOff, PinState::kOff};
    sw.set_pins(center);
    TEST_ASSERT_TRUE(sw.read() == center);
}
