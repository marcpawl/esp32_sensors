// Board GPIO assignments (§2.4).
//
// This header is the *single* definition of the physical pin map, shared by
// every consumer: the firmware's device wiring, the `hal_common` `BoardPins`
// defaults, and (for reference) QEMU/test scenarios. Sharing one definition —
// rather than each layer hard-coding its own numbers — keeps the firmware, the
// HAL defaults, and the spec (§2.4) from drifting apart.
//
// Values follow Specification.md Rev 12 §2.4 (GPIO 34 is input-only, so the
// battery divider must keep its input below ~2.5 V at maximum battery voltage).
#pragma once

namespace thermo::pins {

// §2.4 GPIO assignment.
constexpr int kWireBus = 4;       // 1-Wire bus (DS18B20 x3 + 4.7k pull-up)
constexpr int kRedLed = 16;       // Red LED
constexpr int kGreenLed = 17;     // Green LED
constexpr int kConfig = 1;        // Mode sense: Configure (HIGH == configure)
constexpr int kRun = 2;           // Mode sense: Run (HIGH == run)
constexpr int kBatteryAdc = 34;   // Battery voltage ADC (via divider)
constexpr int kDividerGate = 25;  // Optional divider gate (P-MOSFET control)

}  // namespace thermo::pins
