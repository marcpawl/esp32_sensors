// On-target tests for `espidf_cpp::InterruptHandle`.
//
// These cases run only on the ESP32-S3. They install the real GPIO ISR
// service, register a real handler, drive a GPIO from software, and wait on a
// FreeRTOS semaphore. No HAL stub or fake driver is involved: the point is to
// prove that `InterruptHandle`'s `gpio_isr_handler_remove()` call really
// unregisters the handler.
//
// GPIO 5 is a spare, output-capable pin. The board's assigned pins (1, 2, 4,
// 16, 17, 25, 34) are all in use, and 5 avoids the strapping and USB pins.

#include <cstdint>
#include <optional>
#include <utility>

#include "driver/gpio.h"
#include "esp_err.h"
#include "espidf_cpp/interrupt_handle.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "unity.h"

namespace {

constexpr gpio_num_t kTestGpio = GPIO_NUM_5;
// A second spare, output-capable pin, independent of the first and of the
// board's assigned pins (1, 2, 4, 16, 17, 25, 34), for the independence case.
constexpr gpio_num_t kTestGpio2 = GPIO_NUM_18;
constexpr TickType_t kIsrWaitTicks = pdMS_TO_TICKS(1000);
constexpr TickType_t kNoIsrWaitTicks = pdMS_TO_TICKS(200);

struct IsrState {
    volatile std::uint32_t calls;
    SemaphoreHandle_t semaphore;
    bool level;
    gpio_num_t pin;
};

void isr(void* arg)
{
    auto* state = static_cast<IsrState*>(arg);
    state->calls = state->calls + 1U;
    BaseType_t woken = pdFALSE;
    (void)xSemaphoreGiveFromISR(state->semaphore, &woken);
    if (woken == pdTRUE) {
        portYIELD_FROM_ISR(woken);
    }
}

void install_isr_service()
{
    // The service is process-wide, so a second test sees INVALID_STATE.
    const esp_err_t err = gpio_install_isr_service(0);
    TEST_ASSERT_TRUE(err == ESP_OK || err == ESP_ERR_INVALID_STATE);
}

void configure_pin(IsrState& state, gpio_num_t pin = kTestGpio)
{
    state.calls = 0;
    state.level = false;
    state.pin = pin;
    state.semaphore = xSemaphoreCreateBinary();
    TEST_ASSERT_NOT_NULL(state.semaphore);

    install_isr_service();

    gpio_config_t config = {};
    config.pin_bit_mask = 1ULL << static_cast<int>(pin);
    config.mode = GPIO_MODE_INPUT_OUTPUT;
    config.pull_up_en = GPIO_PULLUP_DISABLE;
    config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    // Leave the interrupt disabled while establishing a known level. If it
    // were enabled here, the level change could fire (and be counted) before
    // the handler under test is installed.
    config.intr_type = GPIO_INTR_DISABLE;
    TEST_ASSERT_EQUAL(ESP_OK, gpio_config(&config));
    TEST_ASSERT_EQUAL(ESP_OK, gpio_set_level(pin, 0));
    TEST_ASSERT_EQUAL(ESP_OK, gpio_set_intr_type(pin, GPIO_INTR_ANYEDGE));
    TEST_ASSERT_EQUAL(ESP_OK, gpio_isr_handler_add(pin, isr, &state));

    // Discard anything that arrived while the handler was being installed.
    (void)xSemaphoreTake(state.semaphore, 0);
    state.calls = 0;
}

void toggle(IsrState& state)
{
    state.level = !state.level;
    TEST_ASSERT_EQUAL(ESP_OK, gpio_set_level(state.pin, state.level ? 1 : 0));
}

void expect_isr(IsrState& state)
{
    // Drain a token left by an earlier edge.
    (void)xSemaphoreTake(state.semaphore, 0);
    state.calls = 0;
    toggle(state);
    TEST_ASSERT_EQUAL(pdTRUE, xSemaphoreTake(state.semaphore, kIsrWaitTicks));
    TEST_ASSERT_EQUAL_UINT32(1U, state.calls);
}

void expect_no_isr(IsrState& state)
{
    // Drain a token left by an earlier assertion.
    (void)xSemaphoreTake(state.semaphore, 0);
    state.calls = 0;
    toggle(state);
    TEST_ASSERT_EQUAL(pdFALSE,
                      xSemaphoreTake(state.semaphore, kNoIsrWaitTicks));
    TEST_ASSERT_EQUAL_UINT32(0U, state.calls);
}

void cleanup(IsrState& state)
{
    (void)gpio_set_intr_type(state.pin, GPIO_INTR_DISABLE);
    (void)gpio_reset_pin(state.pin);
    vSemaphoreDelete(state.semaphore);
}

}  // namespace

TEST_CASE("espidf_cpp InterruptHandle: release removes the real ISR",
          "[espidf_cpp_interrupt_handle]")
{
    using thermo::espidf_cpp::Gpio;
    using thermo::espidf_cpp::InterruptHandle;

    IsrState state{};
    configure_pin(state);

    {
        InterruptHandle handle{Gpio{kTestGpio}};
        TEST_ASSERT_EQUAL_INT(
            static_cast<int>(InterruptHandle::Engaged::kEngaged),
            static_cast<int>(handle.engaged()));
        expect_isr(state);

        handle.release();
        TEST_ASSERT_EQUAL_INT(static_cast<int>(InterruptHandle::Engaged::kIdle),
                              static_cast<int>(handle.engaged()));
    }

    // `gpio_isr_handler_remove()` disables the pin interrupt. Re-enable it so
    // the only possible reason for no ISR is that the handler is gone.
    TEST_ASSERT_EQUAL(ESP_OK, gpio_intr_enable(kTestGpio));
    expect_no_isr(state);
    cleanup(state);
}

TEST_CASE("espidf_cpp InterruptHandle: destructor removes the real ISR",
          "[espidf_cpp_interrupt_handle]")
{
    using thermo::espidf_cpp::Gpio;
    using thermo::espidf_cpp::InterruptHandle;

    IsrState state{};
    configure_pin(state);

    {
        InterruptHandle handle{Gpio{kTestGpio}};
        expect_isr(state);
    }

    TEST_ASSERT_EQUAL(ESP_OK, gpio_intr_enable(kTestGpio));
    expect_no_isr(state);
    cleanup(state);
}

TEST_CASE("espidf_cpp InterruptHandle: move keeps exactly one real ISR",
          "[espidf_cpp_interrupt_handle]")
{
    using thermo::espidf_cpp::Gpio;
    using thermo::espidf_cpp::InterruptHandle;

    IsrState state{};
    configure_pin(state);

    {
        std::optional<InterruptHandle> source;
        source.emplace(Gpio{kTestGpio});
        expect_isr(state);

        InterruptHandle destination{std::move(*source)};
        // Destroying the moved-from source must not remove the registration:
        // the ISR is still the one proof that exactly one handle owns it.
        source.reset();
        expect_isr(state);

        TEST_ASSERT_EQUAL_INT(
            static_cast<int>(InterruptHandle::Engaged::kEngaged),
            static_cast<int>(destination.engaged()));
        destination.release();
        TEST_ASSERT_EQUAL_INT(static_cast<int>(InterruptHandle::Engaged::kIdle),
                              static_cast<int>(destination.engaged()));
    }

    TEST_ASSERT_EQUAL(ESP_OK, gpio_intr_enable(kTestGpio));
    expect_no_isr(state);
    cleanup(state);
}

TEST_CASE(
    "espidf_cpp InterruptHandle: handles on different pins are independent",
    "[espidf_cpp_interrupt_handle]")
{
    using thermo::espidf_cpp::Gpio;
    using thermo::espidf_cpp::InterruptHandle;

    IsrState first{};
    IsrState second{};
    configure_pin(first, kTestGpio);
    configure_pin(second, kTestGpio2);

    {
        InterruptHandle first_handle{Gpio{kTestGpio}};
        InterruptHandle second_handle{Gpio{kTestGpio2}};
        TEST_ASSERT_EQUAL_INT(
            static_cast<int>(InterruptHandle::Engaged::kEngaged),
            static_cast<int>(first_handle.engaged()));
        TEST_ASSERT_EQUAL_INT(
            static_cast<int>(InterruptHandle::Engaged::kEngaged),
            static_cast<int>(second_handle.engaged()));

        expect_isr(first);
        expect_isr(second);

        first_handle.release();
        TEST_ASSERT_EQUAL_INT(static_cast<int>(InterruptHandle::Engaged::kIdle),
                              static_cast<int>(first_handle.engaged()));
        TEST_ASSERT_EQUAL_INT(
            static_cast<int>(InterruptHandle::Engaged::kEngaged),
            static_cast<int>(second_handle.engaged()));

        // Re-enable the first pin's interrupt: `gpio_isr_handler_remove()`
        // disabled it, so the only reason for no ISR is that the handler is
        // gone.
        TEST_ASSERT_EQUAL(ESP_OK, gpio_intr_enable(first.pin));
        expect_no_isr(first);

        // The second handle still owns its pin, so its ISR must still fire.
        expect_isr(second);
    }

    // The second handle's destructor removed its handler.
    TEST_ASSERT_EQUAL(ESP_OK, gpio_intr_enable(second.pin));
    expect_no_isr(second);
    cleanup(first);
    cleanup(second);
}
