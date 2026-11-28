#include "simulator/restore_hardware.hpp"

#include <cstdint>

#include "espidf_cpp/nvs.hpp"
#include "espidf_cpp/softswitch.hpp"
#include "espidf_cpp/switch.hpp"
#include "logic/pin_map.hpp"

namespace thermo::simulator {
namespace {

constexpr espidf_cpp::SwitchState to_switch_state(std::uint8_t stored) noexcept
{
    return stored == kOn ? espidf_cpp::SwitchState::kOn
                         : espidf_cpp::SwitchState::kOff;
}

}  // namespace

void restore_hardware() noexcept
{
    // The wrapper owns the one-time flash setup, including recovery.
    espidf_cpp::Nvs<espidf_cpp::NvsMode::ReadWrite>::init_nvs_flash();

    // Open read-write even though the values are only read: a read-only open
    // fails when the namespace has never been written (a first boot).
    espidf_cpp::Nvs<espidf_cpp::NvsMode::ReadWrite> nvs{kSwitchNamespace};
    const std::uint8_t config = nvs.read(kConfigSwitchKey, kOff);
    const std::uint8_t run = nvs.read(kRunSwitchKey, kOff);

    espidf_cpp::SoftSwitch config_switch{logic::pins::kSwitchConfig};
    espidf_cpp::SoftSwitch run_switch{logic::pins::kSwitchRun};
    config_switch.set(to_switch_state(config));
    run_switch.set(to_switch_state(run));
}

}  // namespace thermo::simulator
