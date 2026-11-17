// Real switch implementation for the dev board. Configures the GPIO as an
// input with a pull-up and reads the pin level; no debouncing or edge
// interpretation happens here.
//
// Mapping: a closed switch reads `SwitchState::kOn`, an open switch reads
// `SwitchState::kOff`.
//
// Interrupts: `set_interrupt()` installs a GPIO ISR on any edge. The ISR runs
// in interrupt context — it must be short and allocate nothing — so it only
// forwards the new state to the registered callback (IRAM-safe, no logging).
// The state change is detected by comparing the sampled level against the last
// one observed by the ISR.
// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#include "driver/gpio.h"
#include "esp_attr.h"
#pragma GCC diagnostic pop
// NOLINTEND

#include "hal_board/switch.hpp"

namespace thermo::hal::board {
namespace {

gpio_config_t input_config(int gpio) {
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = 1ULL << gpio;
    cfg.mode = GPIO_MODE_INPUT;
    // The switch is expected to short the pin to ground when closed, so the
    // internal pull-up keeps the open reading a defined high.
    cfg.pull_up_en = GPIO_PULLUP_ENABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.intr_type = GPIO_INTR_DISABLE;
    return cfg;
}

// Maps a raw pin level to a state (active-low: low == closed).
IRAM_ATTR SwitchState state_from_level(int level) noexcept {
    return level == 0 ? SwitchState::kOn : SwitchState::kOff;
}

// Callback registered via `set_interrupt()` (or empty).
SwitchHandler g_handler = std::nullopt;

// Last state observed by the ISR, used to detect changes. Only the ISR reads
// or writes it, so no synchronisation is needed.
SwitchState g_last_state = SwitchState::kOff;

// The pin the ISR receives through ESP-IDF's `void*` argument.
// `gpio_isr_handler_add()` carries no typed payload, and casting the pin number
// to a pointer (an int-to-pointer cast) pessimizes the optimizer, so the value
// lives in static storage and its address is what travels. Static storage also
// keeps it valid for as long as the ISR is installed.
int g_isr_pin = 0;

// GPIO ISR: on any edge, sample the pin and, if the state actually changed,
// invoke the registered callback with the new state.
void IRAM_ATTR switch_isr(void* arg) noexcept {
    const auto gpio = *static_cast<const int*>(arg);
    const SwitchState state =
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

Switch::Switch(int gpio) noexcept : gpio_(gpio) {}

esp_err_t Switch::init() noexcept {
    auto cfg = input_config(gpio_);
    return gpio_config(&cfg);
}

SwitchState Switch::read() const noexcept {
    // Active-low wiring: the pin reads low while the switch is closed, so map
    // that to `kOn` (closed) and the open high to `kOff`.
    return state_from_level(gpio_get_level(static_cast<gpio_num_t>(gpio_)));
}

void Switch::set_interrupt(SwitchHandler handler) noexcept {
    g_handler = handler;
    g_last_state = read();

    auto gpio = static_cast<gpio_num_t>(gpio_);
    if (!handler.has_value()) {
        // Unregistering: disable the ISR on this pin.
        gpio_set_intr_type(gpio, GPIO_INTR_DISABLE);
        gpio_isr_handler_remove(gpio);
        return;
    }
    // Report both edges so the first close and the first open after
    // registration are both delivered.
    g_isr_pin = gpio_;
    gpio_set_intr_type(gpio, GPIO_INTR_ANYEDGE);
    gpio_isr_handler_add(gpio, switch_isr, &g_isr_pin);
}

}  // namespace thermo::hal::board
