// `ToggleSwitch<Board>`: debounced switch state with a change callback.
//
// Part of the `liblogic` component: the behaviour is board-independent. The
// switch and its GPIO handler come from `hal::BoardLike`, so the same code
// runs on the GPIO-backed `hal_board` and on the pin-free `hal_stub`.
//
// The HAL reports edges without semantics. This class adds the semantics a
// caller wants from a switch:
//
//   - the callback runs only after the switch has been still for the debounce
//     window, so a bouncing contact reports once;
//   - a burst that never settles within the stability window is a hardware
//     fault, logged with the pin and aborted;
//   - the callback receives the settled state, so it does not have to read the
//     switch again from inside the callback.
//
// The GPIO handler is ISR-safe: it reads the switch, records the state, and
// restarts the debounce timer (`Timer::restart()` is ISR-safe). Everything
// else — logging, the settled-state lock, and the user callback — runs in the
// esp_timer task.
#pragma once

// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#pragma GCC diagnostic pop
// NOLINTEND

#include <chrono>
#include <functional>
#include <optional>
#include <utility>

#include <cassert>
#include <cstdint>
#include <cstdlib>

#include "espidf_cpp/mutex.hpp"
#include "espidf_cpp/timer.hpp"
#include "hal/board_types.hpp"
#include "hal/switch_types.hpp"
#include "utility/isr_safe.hpp"

namespace thermo {

namespace toggle {

// Default debounce and stability windows (issue #51).
inline constexpr std::chrono::milliseconds kDebounce{50};
inline constexpr std::chrono::milliseconds kStability{10'000};

// Whether a burst that began at `start_us` has lasted at least `stability` at
// `now_us`. Pure, so the boundary is host-testable without a timer.
[[nodiscard]] constexpr bool burst_exceeded(
    std::int64_t now_us, std::int64_t start_us,
    std::chrono::milliseconds stability) noexcept {
    return now_us - start_us >=
           std::chrono::duration_cast<std::chrono::microseconds>(stability)
               .count();
}

}  // namespace toggle

// Watches one HAL switch and reports its debounced state.
//
// A `ToggleSwitch` lives for the firmware's lifetime in `mcgowan` and `donny`.
// Unit tests may create and destroy it freely: the destructor unregisters the
// switch interrupt and stops the timer before its state is destroyed.
//
// @tparam Board A type satisfying `hal::BoardLike`; its `SwitchType` is the
//               concrete switch (`hal::SwitchLike`).
template<hal::BoardLike Board>
class ToggleSwitch {
  public:
    using SwitchType = typename Board::SwitchType;
    using CallbackFn = std::function<void(hal::SwitchState)>;
    using State = hal::SwitchState;

    // `sw` must be initialized (`init()`) and must outlive this object.
    // `callback` runs in the esp_timer task, outside the state lock.
    //
    // `debounce` and `stability` exist for host tests: the firmware uses the
    // defaults (`toggle::kDebounce`, `toggle::kStability`).
    ToggleSwitch(SwitchType& sw, CallbackFn callback,
                 std::chrono::milliseconds debounce = toggle::kDebounce,
                 std::chrono::milliseconds stability = toggle::kStability)
        : the_switch_(sw),
          callback_fn_(std::move(callback)),
          debounce_threshold_(debounce),
          stability_threshold_(stability),
          current_state_(sw.read()),
          debounce_timer_([this] { on_debounce(); }, "toggle_switch") {
        assert(callback_fn_ && "ToggleSwitch callback must not be empty");
        the_switch_.set_interrupt([this] { on_interrupt(); });
        (void)debounce_timer_.restart(debounce_threshold_);
    }

    ToggleSwitch(ToggleSwitch const&) = delete;
    ToggleSwitch(ToggleSwitch&&) = delete;
    ToggleSwitch& operator=(ToggleSwitch const&) = delete;
    ToggleSwitch& operator=(ToggleSwitch&&) = delete;

    ~ToggleSwitch() {
        // The board owns the switch and outlives this object, so the
        // interrupt must be cleared here; otherwise the ISR would call into a
        // destroyed ToggleSwitch. `~Timer` stops the timer and waits for an
        // in-flight callback before the state below is destroyed.
        the_switch_.set_interrupt({});
    }

    // The last settled state, or `std::nullopt` until the first debounce
    // window completes. Safe to call from any task, including the callback.
    [[nodiscard]] std::optional<State> get_state() const noexcept {
        espidf_cpp::Lock lock(state_mutex_);
        return previous_state_;
    }

  private:
    static constexpr const char* kTag = "ToggleSwitch";

    // GPIO interrupt: record the new state and restart the debounce timer.
    // This is the only code that runs in interrupt context.
    [[thermo::isr_safe]] void on_interrupt() noexcept;

    // esp_timer task: settle the state once the switch has been still for the
    // debounce window, or abort if the burst never settled.
    void on_debounce() noexcept;

    SwitchType& the_switch_;
    CallbackFn callback_fn_;
    std::chrono::milliseconds debounce_threshold_;
    std::chrono::milliseconds stability_threshold_;

    // The settled state is read by arbitrary tasks, so it uses a FreeRTOS
    // mutex. The burst fields below are shared with the ISR and therefore use
    // a critical section: a mutex must not be taken in an interrupt.
    mutable espidf_cpp::Mutex state_mutex_;
    std::optional<State> previous_state_ = std::nullopt;

    mutable portMUX_TYPE burst_lock_ = portMUX_INITIALIZER_UNLOCKED;
    State current_state_;
    bool pending_ = false;
    bool burst_active_ = false;
    bool unstable_ = false;
    std::int64_t burst_start_us_ = 0;

    // Declared last: destroyed first. `~Timer` waits for an in-flight
    // callback, so it cannot touch the state above after that state is gone.
    espidf_cpp::Timer debounce_timer_;
};

template<hal::BoardLike Board>
[[thermo::isr_safe]] void ToggleSwitch<Board>::on_interrupt() noexcept {
    const State new_state = the_switch_.read();
    const std::int64_t now_us = esp_timer_get_time();

    bool restart = true;
    portENTER_CRITICAL_SAFE(&burst_lock_);
    current_state_ = new_state;
    pending_ = true;
    if (!burst_active_) {
        burst_active_ = true;
        burst_start_us_ = now_us;
    }
    if (toggle::burst_exceeded(now_us, burst_start_us_, stability_threshold_)) {
        // Do not restart the timer: let the pending expiry report the fault
        // from task context. An ISR must not log or abort.
        unstable_ = true;
        restart = false;
    }
    portEXIT_CRITICAL_SAFE(&burst_lock_);

    if (restart) {
        (void)debounce_timer_.restart(debounce_threshold_);
    }
}

template<hal::BoardLike Board>
void ToggleSwitch<Board>::on_debounce() noexcept {
    bool unstable = false;
    bool pending = false;
    State new_state = State::kOff;
    {
        portENTER_CRITICAL_SAFE(&burst_lock_);
        unstable = unstable_;
        pending = pending_;
        if (pending) {
            // An edge raced this expiry; the ISR has already restarted the
            // timer. Re-arm and wait for the switch to be still.
            pending_ = false;
        } else {
            new_state = current_state_;
            burst_active_ = false;
            unstable_ = false;
        }
        portEXIT_CRITICAL_SAFE(&burst_lock_);
    }

    if (unstable) {
        ESP_LOGE(kTag, "switch on GPIO %d never settled", the_switch_.gpio());
        std::abort();
    }

    if (pending) {
        (void)debounce_timer_.restart(debounce_threshold_);
        return;
    }

    std::optional<State> previous;
    {
        espidf_cpp::Lock lock(state_mutex_);
        previous = previous_state_;
        previous_state_ = new_state;
    }

    if (!previous.has_value() || *previous != new_state) {
        // Outside the lock: the callback may call get_state().
        callback_fn_(new_state);
    }
}

}  // namespace thermo
