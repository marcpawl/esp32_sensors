#pragma once

#include <cstdint>

namespace thermo::simulator {

// NVS namespace and keys shared by the save and restore sides. A key that has
// never been written restores as an open pole, the centre/Off position.
constexpr char kSwitchNamespace[] = "simulator";
constexpr char kConfigSwitchKey[] = "switch_config";
constexpr char kRunSwitchKey[] = "switch_run";

// Stored representation of a mode-sense pole: open is 0, closed is 1.
constexpr std::uint8_t kOff = 0U;
constexpr std::uint8_t kOn = 1U;

// Set the simulated hardware to the state it was last in prior to a reboot.
// Simulates physical hardware.
void restore_hardware() noexcept;

}  // namespace thermo::simulator
