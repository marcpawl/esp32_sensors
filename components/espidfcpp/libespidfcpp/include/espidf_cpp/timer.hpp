// RAII wrapper around an ESP-IDF `esp_timer` instance.
//
// The class owns exactly one timer: the constructor creates it, the destructor
// stops and deletes it, and moving transfers ownership. It is non-copyable
// because two owners would delete the same timer.
//
// Durations use `std::chrono::microseconds`; the `esp_timer` C API's plain
// `uint64_t` values are an implementation detail of these member functions.
#pragma once

// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#include "esp_err.h"
#include "esp_timer.h"
#pragma GCC diagnostic pop
// NOLINTEND

#include <chrono>
#include <functional>
#include <memory>
#include <utility>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

#include "utility/isr_safe.hpp"
#include "utility/static_string.hpp"

namespace thermo::espidf_cpp {

using thermo::utility::static_string;

// Maximum stored timer-name length, matching the task-name bound.
inline constexpr std::size_t kTimerNameCapacity = 32;

class Timer {
  public:
    // Creates the timer with the given callback, name, and light-sleep policy.
    // The callback runs in the esp_timer task, so it does *not* have to be
    // ISR-safe (and is not annotated `[[thermo::isr_safe]]`). A failed create,
    // or an empty callback, is fatal because there is no useful object to
    // return.
    Timer(std::function<void()> callback, const char* name = "timer",
          bool skip_unhandled_events = false)
        : callback_(
              std::make_unique<std::function<void()>>(std::move(callback))),
          name_(name != nullptr ? name : "timer")
    {
        if (!*callback_) {
            assert(false && "timer callback must not be empty");
            std::abort();
        }

        esp_timer_create_args_t args = {};
        args.callback = &Timer::invoke;
        args.arg = callback_.get();
        args.dispatch_method = ESP_TIMER_TASK;
        args.name = name_.c_str();
        args.skip_unhandled_events = skip_unhandled_events;

        if (esp_timer_create(&args, &handle_) != ESP_OK) {
            assert(false && "esp_timer_create failed");
            std::abort();
        }
    }

    ~Timer() { destroy(); }

    Timer(Timer const&) = delete;
    Timer& operator=(Timer const&) = delete;

    Timer(Timer&& other) noexcept
        : handle_(std::exchange(other.handle_, nullptr)),
          callback_(std::move(other.callback_)),
          name_(other.name_),
          periodic_(other.periodic_)
    {
    }

    Timer& operator=(Timer&& other) noexcept
    {
        if (this != &other) {
            destroy();
            handle_ = std::exchange(other.handle_, nullptr);
            callback_ = std::move(other.callback_);
            name_ = other.name_;
            periodic_ = other.periodic_;
        }
        return *this;
    }

    // Starts a one-shot timer. The timer must not already be running. Remembers
    // the timer's kind so `restart()` can start it again if it was stopped.
    esp_err_t start_once(std::chrono::microseconds delay)
    {
        const esp_err_t result =
            esp_timer_start_once(handle_, to_microseconds(delay));
        if (result == ESP_OK) {
            periodic_ = false;
        }
        return result;
    }

    // Starts a periodic timer. The timer must not already be running.
    // Remembers the timer's kind so `restart()` can start it again if it was
    // stopped.
    esp_err_t start_periodic(std::chrono::microseconds period)
    {
        const esp_err_t result =
            esp_timer_start_periodic(handle_, to_microseconds(period));
        if (result == ESP_OK) {
            periodic_ = true;
        }
        return result;
    }

    // Restarts a running timer, taking a new delay (and, for a periodic timer,
    // a new period). A timer that is not running is started instead: as a
    // periodic timer with `delay` as its new period when it has one, and as a
    // one-shot timer otherwise.
    //
    // ISR-safe: this path calls only `esp_timer_restart()` and, for a timer
    // that is not running, the `esp_timer_start_*()` entry points. It does not
    // query the timer or allocate. (`is_active()` and `period()` are for task
    // context; they are not used here.)
    [[thermo::isr_safe]] esp_err_t restart(std::chrono::microseconds delay)
    {
        // `esp_timer_restart()` reports ESP_ERR_INVALID_STATE when the timer is
        // not running. That is the only case that falls back to a start; any
        // other result is the caller's to see.
        const esp_err_t result =
            esp_timer_restart(handle_, to_microseconds(delay));
        if (result != ESP_ERR_INVALID_STATE) {
            return result;
        }
        return periodic_ ? start_periodic(delay) : start_once(delay);
    }

    // Disarms the timer without waiting for an in-flight callback.
    esp_err_t stop() { return esp_timer_stop(handle_); }

    // Disarms the timer and waits up to `timeout_ticks` for an in-flight
    // callback to finish.
    esp_err_t stop_blocking(std::uint32_t timeout_ticks)
    {
        return esp_timer_stop_blocking(handle_, timeout_ticks);
    }

    // Stops the timer, ignoring the result. Provided for callers that only need
    // to disarm it and do not care whether it was running.
    void cancel() noexcept { (void)esp_timer_stop(handle_); }

    [[nodiscard]] bool is_active() const
    {
        return handle_ != nullptr && esp_timer_is_active(handle_);
    }

    // The timer's period: 0 for a one-shot timer.
    [[nodiscard]] std::chrono::microseconds period() const
    {
        std::uint64_t period_us = 0;
        const esp_err_t result = esp_timer_get_period(handle_, &period_us);
        assert(result == ESP_OK && "esp_timer_get_period failed");
        (void)result;
        return std::chrono::microseconds{period_us};
    }

    // The next expiry time of a one-shot timer. A periodic timer has no
    // meaningful expiry here, so this asserts instead of returning a misleading
    // value.
    [[nodiscard]] std::chrono::microseconds expiry() const
    {
        std::uint64_t expiry_us = 0;
        const esp_err_t result = esp_timer_get_expiry_time(handle_, &expiry_us);
        assert(result == ESP_OK && "esp_timer_get_expiry_time failed");
        (void)result;
        return std::chrono::microseconds{expiry_us};
    }

    [[nodiscard]] esp_timer_handle_t handle() const noexcept { return handle_; }

  private:
    // The `esp_timer` C callback, bridging back to the stored std::function.
    static void invoke(void* arg)
    {
        (*static_cast<std::function<void()>*>(arg))();
    }

    static std::uint64_t to_microseconds(std::chrono::microseconds value)
    {
        if (value < std::chrono::microseconds::zero()) {
            assert(false && "timer duration must not be negative");
            std::abort();
        }
        return static_cast<std::uint64_t>(value.count());
    }

    void destroy() noexcept
    {
        if (handle_ == nullptr) {
            return;
        }
        (void)esp_timer_stop_blocking(handle_, UINT32_MAX);
        (void)esp_timer_delete(handle_);
        handle_ = nullptr;
    }

    esp_timer_handle_t handle_ = nullptr;
    // Heap-backed so its address stays stable when the Timer moves; that
    // address is what esp_timer passes back to `invoke`.
    std::unique_ptr<std::function<void()>> callback_;
    static_string<kTimerNameCapacity> name_;
    // Whether this timer was last started as a periodic timer. `restart()`
    // needs it to choose the start call when the timer is not running, without
    // querying the timer (which is not ISR-safe on the host stub).
    bool periodic_ = false;
};

}  // namespace thermo::espidf_cpp
