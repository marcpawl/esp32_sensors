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
#include <utility>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

#include "espidf_cpp/static_string.hpp"

namespace thermo::espidf_cpp {

// Maximum stored timer-name length, matching the task-name bound.
inline constexpr std::size_t kTimerNameCapacity = 32;

class Timer {
  public:
    // Creates the timer with the given callback, argument, name, and
    // light-sleep policy. The callback runs in the esp_timer task. A failed
    // create is fatal because there is no useful object to return.
    Timer(esp_timer_cb_t callback, void* arg, const char* name = "timer",
          bool skip_unhandled_events = false)
        : name_(name != nullptr ? name : "timer") {
        esp_timer_create_args_t args = {};
        args.callback = callback;
        args.arg = arg;
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
          name_(std::move(other.name_)) {}

    Timer& operator=(Timer&& other) noexcept {
        if (this != &other) {
            destroy();
            handle_ = std::exchange(other.handle_, nullptr);
            name_ = std::move(other.name_);
        }
        return *this;
    }

    // Starts a one-shot timer. The timer must not already be running.
    esp_err_t start_once(std::chrono::microseconds delay) {
        return esp_timer_start_once(handle_, to_microseconds(delay));
    }

    // Starts a periodic timer. The timer must not already be running.
    esp_err_t start_periodic(std::chrono::microseconds period) {
        return esp_timer_start_periodic(handle_, to_microseconds(period));
    }

    // Restarts a running timer, taking a new delay (and, for a periodic timer,
    // a new period).
    esp_err_t restart(std::chrono::microseconds delay) {
        return esp_timer_restart(handle_, to_microseconds(delay));
    }

    // Disarms the timer without waiting for an in-flight callback.
    esp_err_t stop() { return esp_timer_stop(handle_); }

    // Disarms the timer and waits up to `timeout_ticks` for an in-flight
    // callback to finish.
    esp_err_t stop_blocking(std::uint32_t timeout_ticks) {
        return esp_timer_stop_blocking(handle_, timeout_ticks);
    }

    // Stops the timer, ignoring the result. Provided for callers that only need
    // to disarm it and do not care whether it was running.
    void cancel() noexcept { (void)esp_timer_stop(handle_); }

    [[nodiscard]] bool is_active() const {
        return handle_ != nullptr && esp_timer_is_active(handle_);
    }

    // The timer's period: 0 for a one-shot timer.
    [[nodiscard]] std::chrono::microseconds period() const {
        std::uint64_t period_us = 0;
        const esp_err_t result = esp_timer_get_period(handle_, &period_us);
        assert(result == ESP_OK && "esp_timer_get_period failed");
        (void)result;
        return std::chrono::microseconds{period_us};
    }

    // The next expiry time of a one-shot timer. A periodic timer has no
    // meaningful expiry here, so this asserts instead of returning a misleading
    // value.
    [[nodiscard]] std::chrono::microseconds expiry() const {
        std::uint64_t expiry_us = 0;
        const esp_err_t result = esp_timer_get_expiry_time(handle_, &expiry_us);
        assert(result == ESP_OK && "esp_timer_get_expiry_time failed");
        (void)result;
        return std::chrono::microseconds{expiry_us};
    }

    [[nodiscard]] esp_timer_handle_t handle() const noexcept { return handle_; }

  private:
    static std::uint64_t to_microseconds(std::chrono::microseconds value) {
        if (value < std::chrono::microseconds::zero()) {
            assert(false && "timer duration must not be negative");
            std::abort();
        }
        return static_cast<std::uint64_t>(value.count());
    }

    void destroy() noexcept {
        if (handle_ == nullptr) {
            return;
        }
        (void)esp_timer_stop_blocking(handle_, UINT32_MAX);
        (void)esp_timer_delete(handle_);
        handle_ = nullptr;
    }

    esp_timer_handle_t handle_ = nullptr;
    static_string<kTimerNameCapacity> name_;
};

}  // namespace thermo::espidf_cpp
