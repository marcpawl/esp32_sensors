// Real one-shot timer implementation for the dev board, on top of `esp_timer`.
//
// Each `register_timer()` allocates one `esp_timer` instance and one
// `std::function` box, arms the timer for the requested delay with
// `ESP_TIMER_TASK` dispatch (the default, so the callback runs in the esp_timer
// task rather than in an ISR), and hands the caller a `TimerHandleBoard` that
// owns both.
//
// Lifetime is the delicate part, because the callback receives a bare `void*`
// while the handle may be destroyed at any time:
//
//   * The `std::function` lives in a `shared_ptr` held by the handle, and the
//     timer's `arg` is a raw pointer to it. The handle outlives every callback
//     invocation because `cancel()` / the destructor disarm the timer with
//     `esp_timer_stop_blocking()`, which — from task context — does not return
//     until no callback is running and none will run. Only then is the
//     `shared_ptr` released.
//   * The callback copies the `std::function` before invoking it. That copy is
//     what keeps this safe when a handle is destroyed from *inside* its own
//     callback (a self-cancel), where `esp_timer_stop_blocking()` returns
//     immediately instead of waiting: the box may be freed mid-callback, so
//     nothing after the copy may touch it.
//   * `esp_timer_delete()` from inside a callback is supported: ESP-IDF defers
//     the actual free to the timer task.
// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#include "esp_err.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#pragma GCC diagnostic pop
// NOLINTEND

#include <cassert>
#include <chrono>
#include <functional>
#include <memory>
#include <utility>

#include "hal/timer_board.hpp"

namespace thermo::hal {
namespace {

// `esp_timer` callback: copies the callable (see the lifetime note above) and
// runs it.
void timer_callback(void* arg) {
    auto const box = static_cast<TimerCallbackBox*>(arg);
    assert(box != nullptr);
    // Copy first: a self-cancelling handle may free the box while this runs.
    std::function<void()> const callback = *box;
    callback();
}

}  // namespace

// --- TimerHandleBoard ------------------------------------------------------

TimerHandleBoard::TimerHandleBoard(
    esp_timer_handle_t timer,
    std::shared_ptr<TimerCallbackBox> callback) noexcept
    : timer_(timer), callback_(std::move(callback)) {}

TimerHandleBoard::TimerHandleBoard(TimerHandleBoard&& other) noexcept
    : timer_(std::exchange(other.timer_, nullptr)),
      callback_(std::move(other.callback_)) {}

TimerHandleBoard::~TimerHandleBoard() {
    cancel();
}

TimerHandleBoard& TimerHandleBoard::operator=(
    TimerHandleBoard&& other) noexcept {
    if (this != &other) {
        // Release this handle's timer before taking the other's.
        cancel();
        timer_ = std::exchange(other.timer_, nullptr);
        callback_ = std::move(other.callback_);
    }
    return *this;
}

void TimerHandleBoard::cancel() noexcept {
    if (timer_ == nullptr) {
        return;
    }

    // Wait for an in-flight callback to finish, so releasing the box below
    // cannot race with it. From the timer task itself (a self-cancel) this
    // returns immediately; the copy in `timer_callback()` covers that case.
    // A timer that already expired is simply not armed any more, which is not
    // an error here.
    (void)esp_timer_stop_blocking(timer_, portMAX_DELAY);
    // Deleting is deferred to the timer task when this runs inside a callback,
    // so it is safe in every context this HAL is used from.
    (void)esp_timer_delete(timer_);
    timer_ = nullptr;
    callback_.reset();
}

// --- OneShotTimerBoard -----------------------------------------------------

TimerHandleBoard OneShotTimerBoard::register_timer(
    std::chrono::milliseconds delay, std::function<void()> callback) {
    // A zero (or negative) delay is a programming error: `esp_timer` would fire
    // immediately, and a dropped timer would leave the caller waiting forever.
    assert(delay > std::chrono::milliseconds::zero());
    assert(callback != nullptr);

    auto box = std::make_shared<TimerCallbackBox>(std::move(callback));

    esp_timer_create_args_t args = {};
    args.callback = &timer_callback;
    // Not owned by esp_timer: the handle keeps the box alive for as long as a
    // callback can run (see the lifetime note at the top of this file).
    args.arg = box.get();
    args.dispatch_method = ESP_TIMER_TASK;
    args.name = "one-shot";

    esp_timer_handle_t timer = nullptr;
    if (esp_timer_create(&args, &timer) != ESP_OK) {
        // No recovery is meaningful: out of memory here means the caller's wait
        // would never end. Report it and give back an empty handle rather than
        // ever returning a handle that does not own a timer.
        assert(false && "esp_timer_create failed");
        return TimerHandleBoard();
    }

    auto const delay_us =
        static_cast<uint64_t>(std::chrono::microseconds(delay).count());
    if (esp_timer_start_once(timer, delay_us) != ESP_OK) {
        // Do not leak the instance on a failed arm.
        (void)esp_timer_delete(timer);
        assert(false && "esp_timer_start_once failed");
        return TimerHandleBoard();
    }

    return TimerHandleBoard(timer, std::move(box));
}

}  // namespace thermo::hal
