// Board GPIO assignments (§2.4).
//
// This header is the *single* definition of the physical pin map, shared by
// every consumer: the firmware's device wiring, the `hal_common` `BoardPins`
// defaults, and (for reference) the host test scenarios. Sharing one
// definition — rather than each layer hard-coding its own numbers — keeps the
// firmware, the HAL defaults, and the spec (§2.4) from drifting apart.
//
// Values follow Specification.md Rev 13 §2.4 (GPIO 34 is input-only, so the
// battery divider must keep its input below ~2.5 V at maximum battery voltage).
//
// The constants carry the shared `espidf_cpp::Gpio` type rather than an `int`,
// so the pin map itself cannot be mixed up with another number. A caller that
// needs the raw pin number (a legacy driver, a bit mask) asks for it with
// `as_gpio_num_t()`.
#pragma once

#include "espidf_cpp/gpio.hpp"

namespace thermo::logic::pins {

// §2.4 GPIO assignment.
constexpr espidf_cpp::Gpio kWireBus{GPIO_NUM_4};       // 1-Wire bus + pull-up
constexpr espidf_cpp::Gpio kRedLed{GPIO_NUM_16};       // Red LED
constexpr espidf_cpp::Gpio kGreenLed{GPIO_NUM_17};     // Green LED
constexpr espidf_cpp::Gpio kSwitchConfig{GPIO_NUM_1};  // Mode sense: Configure
constexpr espidf_cpp::Gpio kSwitchRun{GPIO_NUM_2};     // Mode sense: Run
constexpr espidf_cpp::Gpio kBatteryAdc{GPIO_NUM_34};   // Battery voltage ADC
constexpr espidf_cpp::Gpio kDividerGate{GPIO_NUM_25};  // Optional divider gate

}  // namespace thermo::logic::pins
