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
#include "esp_err.h"
#pragma GCC diagnostic pop
// NOLINTEND

#include <array>
#include <atomic>
#include <utility>

#include <cassert>
#include <cstddef>
#include <cstdint>

#include "espidf_cpp/switch.hpp"

#include "espidf_cpp/gpio.hpp"

namespace thermo::espidf_cpp {
namespace {

// Whether switch_isr() is currently inside the registered callback. Explicitly
// 32-bit because a narrower atomic is not guaranteed lock-free on Xtensa, and
// the ISR runs from IRAM where a call into libatomic's fallback would be fatal.
// The width is a hardware requirement, not a size optimization.
// NOLINTNEXTLINE(performance-enum-size)
enum class HandlerState : std::uint32_t {
    kIdle = 0,
    kRunning,
};

// One slot per GPIO, indexed by the pin number. The slot's address travels
// through `gpio_isr_handler_add()`'s `void*` argument, so the ISR can reach its
// own state without converting an integer to a pointer (which pessimizes the
// optimizer).
struct IsrSlot {
    Gpio gpio;
    SwitchState last_state = SwitchState::kOff;
    // Read by the task that destroys the switch after it has removed the ISR.
    std::atomic<HandlerState> handler_state{HandlerState::kIdle};
    SwitchCallback handler;
};

static_assert(std::atomic<HandlerState>::is_always_lock_free,
              "the ISR must not call a libatomic fallback from IRAM");

// Registered Interupt Service Routine handlers
std::array<IsrSlot, static_cast<std::size_t>(GPIO_NUM_MAX)> g_slots{};

gpio_config_t input_config(espidf_cpp::Gpio gpio) {
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = 1ULL << gpio.as_gpio_num_t();
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
        state_from_level(gpio_get_level(slot.gpio.as_gpio_num_t()));
    if (state == slot.last_state) {
        return;
    }
    slot.last_state = state;
    if (slot.handler) {
        static_assert(noexcept(slot.handler()),
                      "the switch ISR callback must not throw");

        slot.handler_state.store(HandlerState::kRunning,
                                 std::memory_order_release);
        slot.handler();
        slot.handler_state.store(HandlerState::kIdle,
                                 std::memory_order_release);
    }
}

}  // namespace

Switch::Switch(Gpio gpio) noexcept : gpio_(gpio) {
    auto cfg = input_config(gpio_);
    ESP_ERROR_CHECK(gpio_config(&cfg));
}

Switch::Switch(Switch&& other) noexcept : gpio_(other.gpio_) {
    assert(gpio_.as_gpio_num_t() >= 0);
    assert(gpio_.as_gpio_num_t() < GPIO_NUM_MAX);

    // This handle starts idle, so swapping gives `other` an empty handle and
    // leaves this switch as the sole owner of the ISR registration. The
    // moved-from switch's destructor and remove_interrupt() then do nothing.
    std::swap(interrupt_handle_, other.interrupt_handle_);
}

Switch::~Switch() noexcept {
    remove_interrupt();
}

SwitchState Switch::read() const noexcept {
    // Active-low wiring: the pin reads low while the switch is closed, so map
    // that to `kOn` (closed) and the open high to `kOff`.
    return state_from_level(gpio_get_level(gpio_.as_gpio_num_t()));
}

void Switch::set_interrupt(SwitchCallback handler) noexcept {
    if (!handler) {
        // An empty callback is the same request as remove_interrupt().
        remove_interrupt();
        return;
    }

    auto& slot = g_slots[static_cast<std::size_t>(gpio_.as_gpio_num_t())];
    slot.gpio = gpio_;
    slot.handler = std::move(handler);
    slot.last_state = read();

    // Replacing a callback keeps the registration: removing the ISR here would
    // tear down the handler this call is about to own.
    if (interrupt_handle_.engaged() == InterruptHandle::Engaged::kEngaged) {
        return;
    }

    // Report both edges so the first close and the first open after
    // registration are both delivered.
    gpio_set_intr_type(gpio_.as_gpio_num_t(), GPIO_INTR_ANYEDGE);
    ESP_ERROR_CHECK(
        gpio_isr_handler_add(gpio_.as_gpio_num_t(), switch_isr, &slot));
    interrupt_handle_ = InterruptHandle{gpio_};
}

void Switch::remove_interrupt() noexcept {
    // A moved-from switch owns no registration and must not touch the slot:
    // that slot now belongs to the moved-to switch.
    if (interrupt_handle_.engaged() == InterruptHandle::Engaged::kIdle) {
        return;
    }

    // Stop new invocations before checking for one already in progress.
    gpio_set_intr_type(gpio_.as_gpio_num_t(), GPIO_INTR_DISABLE);
    interrupt_handle_.release();

    // A handler entered just before the removal can still be running on
    // either core, and this switch's state is about to be destroyed. That
    // must never happen, so fail loudly before releasing the callback.
    auto& slot = g_slots[gpio_.as_gpio_num_t()];
    assert(slot.handler_state.load(std::memory_order_acquire) ==
           HandlerState::kIdle);

    slot.handler = {};
}

}  // namespace thermo::espidf_cpp
