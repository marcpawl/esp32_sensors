// On-target and host tests for `espidf_cpp::Switch`.
//
// The switch is an ESP-IDF GPIO wrapper: it reads an active-low pin
// (level 0 == closed/on, level != 0 == open/off). On the chip the GPIO driver
// provides the pin; on `linux` `hal_stub` supplies the same API, so the cases
// run on both targets. `SoftSwitch` closes and opens a switch by driving
// that pin level, which `read()` observes on either target.

#include <cstdint>
#include <optional>

#include "driver/gpio.h"
#include "esp_err.h"
#include "espidf_cpp/gpio.hpp"
#include "espidf_cpp/softswitch.hpp"
#include "espidf_cpp/switch.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "unity.h"

namespace {

constexpr gpio_num_t kTestGpio = GPIO_NUM_5;
// A second spare, output-capable pin, independent of the first and of the
// board's assigned pins (1, 2, 4, 16, 17, 25, 34).
constexpr gpio_num_t kTestGpio2 = GPIO_NUM_18;
constexpr TickType_t kSwitchIsrWaitTicks = pdMS_TO_TICKS(1000);
constexpr TickType_t kNoSwitchIsrWaitTicks = pdMS_TO_TICKS(200);

struct SwitchIsrState {
    volatile std::uint32_t calls;
    SemaphoreHandle_t semaphore;
};

void install_switch_isr_service()
{
    // The service is process-wide, so a second test sees INVALID_STATE.
    const esp_err_t err = gpio_install_isr_service(0);
    TEST_ASSERT_TRUE(err == ESP_OK || err == ESP_ERR_INVALID_STATE);
}

void make_pin_drivable(gpio_num_t pin)
{
    // The switch constructor configures the pin as an input. Reconfigure it as
    // input/output so the test can drive the level from software while the
    // switch still reads it back through gpio_get_level.
    gpio_config_t config = {};
    config.pin_bit_mask = 1ULL << static_cast<int>(pin);
    config.mode = GPIO_MODE_INPUT_OUTPUT;
    config.pull_up_en = GPIO_PULLUP_ENABLE;
    config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    config.intr_type = GPIO_INTR_DISABLE;
    TEST_ASSERT_EQUAL(ESP_OK, gpio_config(&config));
}

void cleanup_pin(gpio_num_t pin)
{
    (void)gpio_set_intr_type(pin, GPIO_INTR_DISABLE);
    (void)gpio_reset_pin(pin);
}

void notify_switch_isr(void* arg)
{
    auto* state = static_cast<SwitchIsrState*>(arg);
    state->calls = state->calls + 1U;
    BaseType_t woken = pdFALSE;
    (void)xSemaphoreGiveFromISR(state->semaphore, &woken);
    if (woken == pdTRUE) {
        portYIELD_FROM_ISR(woken);
    }
}

void toggle_test_pin()
{
    const std::uint32_t level = gpio_get_level(kTestGpio) == 0 ? 1U : 0U;
    TEST_ASSERT_EQUAL(ESP_OK, gpio_set_level(kTestGpio, level));
}

void expect_switch_isr(SwitchIsrState& state)
{
    // Drain a token left by an earlier edge.
    (void)xSemaphoreTake(state.semaphore, 0);
    state.calls = 0;
    toggle_test_pin();
    TEST_ASSERT_EQUAL(pdTRUE,
                      xSemaphoreTake(state.semaphore, kSwitchIsrWaitTicks));
    TEST_ASSERT_EQUAL_UINT32(1U, state.calls);
}

void expect_no_switch_isr(SwitchIsrState& state)
{
    // Drain a token left by an earlier assertion.
    (void)xSemaphoreTake(state.semaphore, 0);
    state.calls = 0;
    toggle_test_pin();
    TEST_ASSERT_EQUAL(pdFALSE,
                      xSemaphoreTake(state.semaphore, kNoSwitchIsrWaitTicks));
    TEST_ASSERT_EQUAL_UINT32(0U, state.calls);
}

}  // namespace

TEST_CASE("espidf_cpp Switch: read reflects pin level", "[espidf_cpp_switch]")
{
    using thermo::espidf_cpp::Gpio;
    using thermo::espidf_cpp::SoftSwitch;
    using thermo::espidf_cpp::SwitchState;

    SoftSwitch sw{Gpio{kTestGpio}};
    make_pin_drivable(kTestGpio);

    TEST_ASSERT_EQUAL_INT(static_cast<int>(kTestGpio),
                          static_cast<int>(sw.gpio().as_gpio_num_t()));

    sw.set(SwitchState::kOff);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SwitchState::kOff),
                          static_cast<int>(sw.read()));

    sw.set(SwitchState::kOn);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SwitchState::kOn),
                          static_cast<int>(sw.read()));

    sw.set(SwitchState::kOff);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SwitchState::kOff),
                          static_cast<int>(sw.read()));

    cleanup_pin(kTestGpio);
}

TEST_CASE("espidf_cpp Switch: two switches on different pins are independent",
          "[espidf_cpp_switch]")
{
    using thermo::espidf_cpp::Gpio;
    using thermo::espidf_cpp::SoftSwitch;
    using thermo::espidf_cpp::SwitchState;

    SoftSwitch first{Gpio{kTestGpio}};
    SoftSwitch second{Gpio{kTestGpio2}};
    make_pin_drivable(kTestGpio);
    make_pin_drivable(kTestGpio2);

    first.set(SwitchState::kOff);
    second.set(SwitchState::kOff);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SwitchState::kOff),
                          static_cast<int>(first.read()));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SwitchState::kOff),
                          static_cast<int>(second.read()));

    first.set(SwitchState::kOn);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SwitchState::kOn),
                          static_cast<int>(first.read()));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SwitchState::kOff),
                          static_cast<int>(second.read()));

    second.set(SwitchState::kOn);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SwitchState::kOn),
                          static_cast<int>(first.read()));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SwitchState::kOn),
                          static_cast<int>(second.read()));

    first.set(SwitchState::kOff);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SwitchState::kOff),
                          static_cast<int>(first.read()));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SwitchState::kOn),
                          static_cast<int>(second.read()));

    second.set(SwitchState::kOff);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SwitchState::kOff),
                          static_cast<int>(first.read()));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SwitchState::kOff),
                          static_cast<int>(second.read()));

    cleanup_pin(kTestGpio);
    cleanup_pin(kTestGpio2);
}

TEST_CASE("espidf_cpp Switch: move transfers the interrupt registration",
          "[espidf_cpp_switch]")
{
    using thermo::espidf_cpp::Gpio;
    using thermo::espidf_cpp::Switch;

    SwitchIsrState state{};
    state.semaphore = xSemaphoreCreateBinary();
    TEST_ASSERT_NOT_NULL(state.semaphore);
    install_switch_isr_service();

    Switch source{Gpio{kTestGpio}};
    make_pin_drivable(kTestGpio);
    TEST_ASSERT_EQUAL(ESP_OK, gpio_set_level(kTestGpio, 1));
    source.set_interrupt([&state]() noexcept { notify_switch_isr(&state); });
    expect_switch_isr(state);

    std::optional<Switch> destination;
    destination.emplace(std::move(source));

    // The moved-from source owns no registration: asking it to remove one must
    // not tear down the destination's handler, and neither must its destructor.
    // NOLINTNEXTLINE(bugprone-use-after-move)
    source.remove_interrupt();
    expect_switch_isr(state);

    // Only the destination owns the registration, so destroying it removes the
    // handler. Re-enable the pin interrupt so that is the only reason it stops.
    destination.reset();
    TEST_ASSERT_EQUAL(ESP_OK, gpio_intr_enable(kTestGpio));
    expect_no_switch_isr(state);

    cleanup_pin(kTestGpio);
    vSemaphoreDelete(state.semaphore);
}
