#pragma once

// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#include "driver/gpio.h"
#pragma GCC diagnostic pop
// NOLINTEND

#include "espidf_cpp/switch.hpp"

namespace thermo::espidf_cpp {

// A soft switch is not physically connected to the board: software sets it
// on or off by driving the GPIO pin it is built for.
class SoftSwitch : public Switch {
  public:
    // `gpio` is the input pin.
    explicit SoftSwitch(Gpio gpio) noexcept : Switch(gpio) {}

    // Drives the pin to `new_state`: closed (`kOn`) pulls the active-low line
    // low, open (`kOff`) lets it read high. `read()` reports the new state
    // immediately, and a registered handler sees the edge just as it would
    // from hardware. Re-setting the current state is a no-op; no other setting
    // (pin config, interrupt registration, handler) is touched either way.
    void set(SwitchState new_state) noexcept
    {
        if (read() == new_state) {
            return;
        }
        const auto level = new_state == SwitchState::kOn ? 0U : 1U;
        (void)gpio_set_level(gpio().as_gpio_num_t(), level);
    }
};

}  // namespace thermo::espidf_cpp
