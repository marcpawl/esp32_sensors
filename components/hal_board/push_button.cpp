// Real push-button implementation for the dev board. Configures the GPIO as an
// input with a pull-up and reads the pin level; no debouncing or edge
// interpretation happens here.
//
// Mapping: a pressed button reads `ButtonState::kOn`, a released button reads
// `ButtonState::kOff`.
//
// Interrupts: `set_interrupt()` installs a GPIO ISR on any edge. The ISR runs
// in interrupt context — it must be short and allocate nothing — so it only
// forwards the new state to the registered callback (IRAM-safe, no logging).
// The state change is detected by comparing the sampled level against the last
// one observed by the ISR.
#include "hal/push_button.hpp"

#include "driver/gpio.h"
#include "esp_attr.h"

namespace thermo::hal {
namespace {

gpio_config_t input_config(int gpio) {
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = 1ULL << gpio;
    cfg.mode = GPIO_MODE_INPUT;
    // The button is expected to short the pin to ground when pressed, so the
    // internal pull-up keeps the released reading a defined high.
    cfg.pull_up_en = GPIO_PULLUP_ENABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.intr_type = GPIO_INTR_DISABLE;
    return cfg;
}

// Maps a raw pin level to a state (active-low: low == pressed).
IRAM_ATTR ButtonState state_from_level(int level) noexcept {
    return level == 0 ? ButtonState::kOn : ButtonState::kOff;
}

// Callback registered via `set_interrupt()` (or empty).
ButtonHandler g_handler = std::nullopt;

// Last state observed by the ISR, used to detect changes. Only the ISR reads
// or writes it, so no synchronisation is needed.
ButtonState g_last_state = ButtonState::kOff;

// GPIO ISR: on any edge, sample the pin and, if the state actually changed,
// invoke the registered callback with the new state.
void IRAM_ATTR button_isr(void* arg) noexcept {
    const auto gpio = static_cast<int>(reinterpret_cast<intptr_t>(arg));
    const ButtonState state =
        state_from_level(gpio_get_level(static_cast<gpio_num_t>(gpio)));
    if (state == g_last_state) {
        return;
    }
    g_last_state = state;
    if (g_handler.has_value()) {
        g_handler.value()(state);
    }
}

}  // namespace

PushButton::PushButton(int gpio) noexcept : gpio_(gpio) {}

esp_err_t PushButton::init() noexcept {
    auto cfg = input_config(gpio_);
    return gpio_config(&cfg);
}

ButtonState PushButton::read() const noexcept {
    // Active-low wiring: the pin reads low while the button is pressed, so map
    // that to `kOn` (pressed) and the released high to `kOff`.
    return state_from_level(gpio_get_level(static_cast<gpio_num_t>(gpio_)));
}

void PushButton::set_interrupt(ButtonHandler handler) noexcept {
    g_handler = handler;
    g_last_state = read();

    auto gpio = static_cast<gpio_num_t>(gpio_);
    if (!handler.has_value()) {
        // Unregistering: disable the ISR on this pin.
        gpio_set_intr_type(gpio, GPIO_INTR_DISABLE);
        gpio_isr_handler_remove(gpio);
        return;
    }
    // Report both edges so the first press and the first release after
    // registration are both delivered.
    gpio_set_intr_type(gpio, GPIO_INTR_ANYEDGE);
    gpio_isr_handler_add(gpio, button_isr,
                         reinterpret_cast<void*>(static_cast<intptr_t>(gpio_)));
}

}  // namespace thermo::hal
