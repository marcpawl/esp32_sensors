// Host tests for `simulator::restore_hardware()`.
//
// The NVS side is real: values are written with `espidf_cpp::Nvs`, then the
// restore function reads them back and drives the two mode-sense pins. One case
// writes bools the way the console does, so the save format and the restore
// side stay in step. `hal_stub` models the GPIO levels, so the effect is
// observable without hardware.
#include "unity.h"
#include "unity_test_runner.h"

// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#include "driver/gpio.h"
#include "nvs.h"
#pragma GCC diagnostic pop
// NOLINTEND

#include <cstdint>

#include "espidf_cpp/nvs.hpp"
#include "espidf_cpp/switch.hpp"
#include "logic/pin_map.hpp"
#include "simulator/restore_hardware.hpp"

namespace {

using thermo::espidf_cpp::Nvs;
using thermo::espidf_cpp::NvsMode;
using thermo::espidf_cpp::SwitchState;
using thermo::logic::pins::kSwitchConfig;
using thermo::logic::pins::kSwitchRun;
using thermo::simulator::kConfigSwitchKey;
using thermo::simulator::kRunSwitchKey;
using thermo::simulator::kSwitchNamespace;

constexpr std::uint8_t kOff = 0U;
constexpr std::uint8_t kOn = 1U;

// The two mode-sense pins come from the same map the component drives.
constexpr gpio_num_t kConfigPin = kSwitchConfig.as_gpio_num_t();
constexpr gpio_num_t kRunPin = kSwitchRun.as_gpio_num_t();

void init_nvs()
{
    Nvs<NvsMode::ReadWrite>::init_nvs_flash();
}

void clear_switches()
{
    nvs_handle_t handle = 0;
    ESP_ERROR_CHECK(nvs_open(kSwitchNamespace, NVS_READWRITE, &handle));
    ESP_ERROR_CHECK(nvs_erase_all(handle));
    ESP_ERROR_CHECK(nvs_commit(handle));
    nvs_close(handle);
}

void store(gpio_num_t pin, std::uint8_t state)
{
    Nvs<NvsMode::ReadWrite> nvs{kSwitchNamespace};
    nvs.write(pin == kConfigPin ? kConfigSwitchKey : kRunSwitchKey, state);
}

void store_bool(char const* key, bool state)
{
    Nvs<NvsMode::ReadWrite> nvs{kSwitchNamespace};
    nvs.write(key, state);
}

// Switch poles are active-low: driving 0 closes one, driving 1 opens it.
void drive(gpio_num_t pin, SwitchState state)
{
    (void)gpio_set_level(pin, state == SwitchState::kOn ? 0U : 1U);
}

SwitchState read(gpio_num_t pin)
{
    return gpio_get_level(pin) == 0 ? SwitchState::kOn : SwitchState::kOff;
}

void expect_poles(SwitchState config, SwitchState run)
{
    TEST_ASSERT_EQUAL_INT(static_cast<int>(config),
                          static_cast<int>(read(kConfigPin)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(run),
                          static_cast<int>(read(kRunPin)));
}

}  // namespace

TEST_CASE("restore_hardware: an empty namespace opens both poles",
          "[restore_hardware]")
{
    init_nvs();
    clear_switches();
    drive(kConfigPin, SwitchState::kOn);
    drive(kRunPin, SwitchState::kOn);

    thermo::simulator::restore_hardware();

    expect_poles(SwitchState::kOff, SwitchState::kOff);
}

TEST_CASE("restore_hardware: saved positions drive both poles",
          "[restore_hardware]")
{
    init_nvs();
    clear_switches();
    store(kConfigPin, kOn);
    store(kRunPin, kOff);
    drive(kConfigPin, SwitchState::kOff);
    drive(kRunPin, SwitchState::kOn);

    thermo::simulator::restore_hardware();

    expect_poles(SwitchState::kOn, SwitchState::kOff);
}

TEST_CASE("restore_hardware: console bool writes drive both poles",
          "[restore_hardware]")
{
    init_nvs();
    clear_switches();
    store_bool(kConfigSwitchKey, true);
    store_bool(kRunSwitchKey, false);
    drive(kConfigPin, SwitchState::kOff);
    drive(kRunPin, SwitchState::kOn);

    thermo::simulator::restore_hardware();

    expect_poles(SwitchState::kOn, SwitchState::kOff);
}

TEST_CASE("restore_hardware: a missing key opens its own pole only",
          "[restore_hardware]")
{
    init_nvs();
    clear_switches();
    store(kConfigPin, kOn);
    drive(kConfigPin, SwitchState::kOff);
    drive(kRunPin, SwitchState::kOn);

    thermo::simulator::restore_hardware();

    expect_poles(SwitchState::kOn, SwitchState::kOff);
}

TEST_CASE("restore_hardware: repeated calls keep the saved positions",
          "[restore_hardware]")
{
    init_nvs();
    clear_switches();
    store(kConfigPin, kOff);
    store(kRunPin, kOn);

    thermo::simulator::restore_hardware();
    thermo::simulator::restore_hardware();

    expect_poles(SwitchState::kOff, SwitchState::kOn);
}
