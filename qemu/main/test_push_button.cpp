// Native link check for the push-button HAL (`hal_stub`).
//
// Proves the stub push button links and works under real FreeRTOS on the linux
// target: create/init plus one press/release round-trip. The host suite in
// `test/` covers the interrupt callback and the `PushButtonLike` concept in
// depth.
#include "unity.h"

#include "hal/push_button.hpp"
#include "hal/push_button_types.hpp"

namespace {
using thermo::hal::ButtonState;
using thermo::hal::PushButton;
}  // namespace

TEST_CASE("qemu push button: create/init and reads a press/release",
          "[qemu][push_button]") {
    static_assert(thermo::hal::PushButtonLike<PushButton>);

    PushButton button = PushButton::create(/*gpio=*/0);
    TEST_ASSERT_EQUAL(ESP_OK, button.init());

    button.set(ButtonState::kOn);
    TEST_ASSERT_EQUAL(static_cast<int>(ButtonState::kOn),
                      static_cast<int>(button.read()));

    button.set(ButtonState::kOff);
    TEST_ASSERT_EQUAL(static_cast<int>(ButtonState::kOff),
                      static_cast<int>(button.read()));
}
