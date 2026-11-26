// Maps the two mode-sense pole states onto an operating mode (§3).
//
// The board exposes the 3-position ON-OFF-ON switch (a DPDT, §2.3) as two
// independent `Switch` poles — `config_switch()` and `run_switch()` — one
// per mode-sense pole. Interpreting that pair is logic, not hardware, so it
// lives here in the `liblogic` component rather than in the HAL.
//
// This header is deliberately free of chip-specific ESP-IDF dependencies (and
// of `reboot()`), so the mapping is pure, `constexpr`, and host-testable. The
// hardware-fault case is reported as `std::nullopt` instead of rebooting: the
// caller decides how to fail loudly.
#pragma once

// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#pragma GCC diagnostic pop
// NOLINTEND

#include <optional>

#include "espidf_cpp/switch.hpp"

#include "logic/mode.hpp"

namespace thermo::logic {

// The selected mode, or `std::nullopt` when the pole states are impossible for
// a healthy switch (both poles energized — miswired or shorted).
using ModeSelection = std::optional<Mode>;

// Maps the two mode-sense pole states to a mode (§3.1-§3.3).
//
// A healthy DPDT ON-OFF-ON switch energizes at most one sense pole:
//   - Configure pole on  → left position  → `Mode::kConfigure` (§3.2)
//   - Run pole on        → right position → `Mode::kRun` (§3.3)
//   - neither on         → center         → `Mode::kOff` (§3.1)
//   - both on            → hardware fault → `std::nullopt`
//
// Both-off is reported as `kOff` rather than as an error: the center position
// cuts power to the regulator, so a running firmware only observes it
// transiently (e.g. while the switch is passing through center). The
// parameters are the two poles in the switch's own order (§3.1-§3.3):
// Configure pole, then Run pole.
[[nodiscard]] constexpr ModeSelection mode_from_switches(
    espidf_cpp::SwitchState config, espidf_cpp::SwitchState run) noexcept {
    const bool config_on = config == espidf_cpp::SwitchState::kOn;
    const bool run_on = run == espidf_cpp::SwitchState::kOn;
    if (config_on && !run_on) {
        return Mode::kConfigure;
    }
    if (!config_on && run_on) {
        return Mode::kRun;
    }
    if (!config_on && !run_on) {
        return Mode::kOff;
    }
    // Both poles energized: the switch is miswired or shorted.
    return std::nullopt;
}

}  // namespace thermo::logic
