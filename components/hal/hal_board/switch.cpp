// Real switch implementation for the dev board. Configures the GPIO as an
// input with a pull-up and reads the pin level; no debouncing or edge
// interpretation happens here.
//
// Mapping: a closed switch reads `SwitchState::kOn`, an open switch reads
// `SwitchState::kOff`.
//
// Interrupts: `set_interrupt()` installs a GPIO ISR on any edge. The ISR runs
// in interrupt context — it must be short and allocate nothing — so it only
// reports the edge to the registered callback (IRAM-safe, no logging). Each
// GPIO has its own slot, so several switches can register handlers
// independently.
// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#include "driver/gpio.h"
#include "esp_attr.h"
#pragma GCC diagnostic pop
// NOLINTEND

#include <array>
#include <utility>

#include <cassert>
#include <cstddef>

#include "hal_board/switch.hpp"

namespace thermo::hal::board {
namespace {

// One slot per GPIO, indexed by the pin number. The slot's address travels
// through `gpio_isr_handler_add()`'s `void*` argument, so the ISR can reach its
// own state without converting an integer to a pointer (which pessimizes the
// optimizer).
struct IsrSlot {
    int gpio = 0;
    SwitchState last_state = SwitchState::kOff;
    SwitchCallback handler;
};

std::array<IsrSlot, static_cast<std::size_t>(GPIO_NUM_MAX)> g_slots{};

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

// GPIO ISR: on any edge, sample the pin and, if the state actually changed,
// invoke the registered callback. The callback reads the new state itself.
void IRAM_ATTR switch_isr(void* arg) noexcept {
    auto& slot = *static_cast<IsrSlot*>(arg);
    const SwitchState state =
        state_from_level(gpio_get_level(static_cast<gpio_num_t>(slot.gpio)));
    if (state == slot.last_state) {
        return;
    }
    slot.last_state = state;
    if (slot.handler) {
        slot.handler();
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

void Switch::set_interrupt(SwitchCallback handler) noexcept {
    assert(gpio_ >= 0 && gpio_ < GPIO_NUM_MAX);
    auto& slot = g_slots[static_cast<std::size_t>(gpio_)];
    slot.gpio = gpio_;
    slot.handler = std::move(handler);
    slot.last_state = read();

    auto gpio = static_cast<gpio_num_t>(gpio_);
    if (!slot.handler) {
        // Unregistering: disable the ISR on this pin.
        gpio_set_intr_type(gpio, GPIO_INTR_DISABLE);
        gpio_isr_handler_remove(gpio);
        return;
    }
    // Report both edges so the first close and the first open after
    // registration are both delivered.
    gpio_set_intr_type(gpio, GPIO_INTR_ANYEDGE);
    gpio_isr_handler_add(gpio, switch_isr, &slot);
}

}  // namespace thermo::hal::board
