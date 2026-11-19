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
// The constants carry the shared `hal::Gpio` type rather than an `int`, so the
// pin map itself cannot be mixed up with another number. A caller that needs
// the raw pin number (a legacy driver, a bit mask) asks for it with
// `as_int()`.
#pragma once

#include "hal/gpio.hpp"

namespace thermo::pins {

// §2.4 GPIO assignment.
constexpr hal::Gpio kWireBus{4};    // 1-Wire bus (DS18B20 x3 + 4.7k pull-up)
constexpr hal::Gpio kRedLed{16};    // Red LED
constexpr hal::Gpio kGreenLed{17};  // Green LED
constexpr hal::Gpio kConfig{1};     // Mode sense: Configure (HIGH == configure)
constexpr hal::Gpio kRun{2};        // Mode sense: Run (HIGH == run)
constexpr hal::Gpio kBatteryAdc{34};   // Battery voltage ADC (via divider)
constexpr hal::Gpio kDividerGate{25};  // Optional divider gate (P-MOSFET)

}  // namespace thermo::pins
