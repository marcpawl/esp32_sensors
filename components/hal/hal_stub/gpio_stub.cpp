// Mock of the ESP-IDF GPIO ISR API for the linux target.
//
// The shared libespidfcpp tests register a real ISR callback and drive a pin
// from software. On the chip that uses the ESP-IDF driver; on the host this
// file models just enough of the driver to run the same cases: registration
// remembers the callback, level changes invoke it for edge interrupts, and
// removal forgets it. No chip registers are involved.
//
// ESP-IDF's `esp_driver_gpio` registers an empty component on the `linux`
// target, so neither its sources nor its headers are available there. The
// mirrored `driver/gpio.h` supplies the declarations.
// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#include "driver/gpio.h"
#include "esp_err.h"
#pragma GCC diagnostic pop
// NOLINTEND

#include <array>
#include <cstddef>

namespace {

struct PinState {
    int level = 0;
    gpio_int_type_t intr_type = GPIO_INTR_DISABLE;
    bool enabled = false;
    gpio_isr_t handler = nullptr;
    void* arg = nullptr;
};

std::array<PinState, GPIO_NUM_MAX>& pin_states()
{
    static std::array<PinState, GPIO_NUM_MAX> states;
    return states;
}

bool valid(gpio_num_t pin)
{
    return pin >= GPIO_NUM_0 && pin < GPIO_NUM_MAX;
}

PinState& state_for(gpio_num_t pin)
{
    return pin_states()[static_cast<std::size_t>(pin)];
}

bool edge_matches(gpio_int_type_t type, int level)
{
    if (type == GPIO_INTR_ANYEDGE) {
        return true;
    }
    if (type == GPIO_INTR_POSEDGE) {
        return level != 0;
    }
    if (type == GPIO_INTR_NEGEDGE) {
        return level == 0;
    }
    return false;
}

}  // namespace

extern "C" esp_err_t gpio_install_isr_service(int intr_alloc_flags)
{
    (void)intr_alloc_flags;
    return ESP_OK;
}

extern "C" esp_err_t gpio_config(const gpio_config_t* config)
{
    if (config == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    for (int pin = GPIO_NUM_0; pin < GPIO_NUM_MAX; ++pin) {
        if ((config->pin_bit_mask & (1ULL << pin)) == 0U) {
            continue;
        }
        auto& state = state_for(static_cast<gpio_num_t>(pin));
        state.intr_type = config->intr_type;
        state.enabled = config->intr_type != GPIO_INTR_DISABLE;
    }
    return ESP_OK;
}

extern "C" esp_err_t gpio_reset_pin(gpio_num_t pin)
{
    if (!valid(pin)) {
        return ESP_ERR_INVALID_ARG;
    }
    state_for(pin) = PinState{};
    return ESP_OK;
}

extern "C" esp_err_t gpio_set_level(gpio_num_t pin, uint32_t level)
{
    if (!valid(pin)) {
        return ESP_ERR_INVALID_ARG;
    }
    auto& state = state_for(pin);
    const int next = level == 0U ? 0 : 1;
    const bool changed = state.level != next;
    state.level = next;
    if (changed && state.enabled && state.handler != nullptr &&
        edge_matches(state.intr_type, next)) {
        state.handler(state.arg);
    }
    return ESP_OK;
}

extern "C" int gpio_get_level(gpio_num_t pin)
{
    if (!valid(pin)) {
        return 0;
    }
    return state_for(pin).level;
}

extern "C" esp_err_t gpio_set_intr_type(gpio_num_t pin,
                                        gpio_int_type_t intr_type)
{
    if (!valid(pin)) {
        return ESP_ERR_INVALID_ARG;
    }
    state_for(pin).intr_type = intr_type;
    return ESP_OK;
}

extern "C" esp_err_t gpio_intr_enable(gpio_num_t pin)
{
    if (!valid(pin)) {
        return ESP_ERR_INVALID_ARG;
    }
    state_for(pin).enabled = true;
    return ESP_OK;
}

extern "C" esp_err_t gpio_isr_handler_add(gpio_num_t pin, gpio_isr_t handler,
                                          void* arg)
{
    if (!valid(pin)) {
        return ESP_ERR_INVALID_ARG;
    }
    auto& state = state_for(pin);
    state.handler = handler;
    state.arg = arg;
    state.enabled = true;
    return ESP_OK;
}

extern "C" esp_err_t gpio_isr_handler_remove(gpio_num_t pin)
{
    if (!valid(pin)) {
        return ESP_ERR_INVALID_ARG;
    }
    auto& state = state_for(pin);
    state.enabled = false;
    state.handler = nullptr;
    state.arg = nullptr;
    return ESP_OK;
}
