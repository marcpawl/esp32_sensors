// Software-driven one-shot timer stub for the host tests, on top of FreeRTOS
// software timers (`freertos/timers.h`).
//
// `esp_timer` has no `linux` implementation, so the stub uses the FreeRTOS
// timer service task ("Tmr Svc") to dispatch the callback instead — the same
// mechanism `test/main/test_freertos_timers.cpp` pins as working on this
// target. Each `register_timer()` allocates one `TimerCallbackBox` (the
// caller's `std::function`), creates a one-shot (`pdFALSE`, i.e. no
// auto-reload) FreeRTOS timer whose `pvTimerID` is that box, starts it, and
// hands the caller a `TimerHandleStub` that owns both.
//
// Lifetime is the delicate part, because the FreeRTOS timer API is
// *asynchronous*: `xTimerStart()`, `xTimerStop()`, and `xTimerDelete()` only
// queue a command for the timer service task, so neither the timer nor its
// callback can be freed the moment `cancel()` returns. The delete is
// therefore followed by an `xTimerPendFunctionCall()` that frees the box.
// FreeRTOS posts that to the *same* command queue, so the timer service task
// processes it strictly after the delete — by which time no callback for that
// timer can run again:
//
//   * The timer service task drains expired timers *before* the command queue,
//     so a callback that was already due runs before the free is reached.
//   * `timer_callback()` copies the callable before invoking it, so a handle
//     destroyed from inside its own callback (a self-cancel) does not free the
//     callable out from under the frame that is running it.
//
// Pooling the free through the queue is what keeps the two ordering hazards
// (free-before-callback and free-during-callback) impossible rather than
// merely unlikely. The cost is that the memory is released a tick or so after
// `cancel()`, not during it — invisible to a correct caller, which was never
// allowed to assume more than the racy contract in `hal/timer_types.hpp`
// anyway.
// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#pragma GCC diagnostic pop
// NOLINTEND

#include <cassert>
#include <chrono>
#include <functional>
#include <utility>

#include <cstdint>

#include "hal/timer_stub.hpp"

namespace thermo::hal {
namespace {

// Frees the box the timer owned. Runs in the timer service task, queued behind
// the timer's own delete command (see the file comment).
void free_callback_box(void* box, std::uint32_t /*unused*/) {
    delete static_cast<TimerCallbackBox*>(box);
}

// FreeRTOS timer callback: copies the callable (so a self-cancelling handle may
// free the box) and runs it.
void timer_callback(TimerHandle_t timer) {
    auto* const box = static_cast<TimerCallbackBox*>(pvTimerGetTimerID(timer));
    assert(box != nullptr);
    // Copy first: `cancel()` from inside the callback frees the box.
    std::function<void()> const callback = *box;
    callback();
}

// Rounds a millisecond delay to whole FreeRTOS ticks.
TickType_t delay_to_ticks(std::chrono::milliseconds delay) {
    assert(delay > std::chrono::milliseconds::zero());
    auto const ticks = static_cast<TickType_t>(pdMS_TO_TICKS(delay.count()));
    // A delay shorter than one tick rounds to zero, which FreeRTOS rejects (and
    // which would leave the caller waiting forever if it did not).
    // `configTICK_RATE_HZ` is 100 on the test target, so anything below 10 ms
    // lands here.
    assert(ticks > 0 && "delay rounds to fewer than one FreeRTOS tick");
    return ticks;
}

}  // namespace

// --- TimerHandleStub -------------------------------------------------------

TimerHandleStub::TimerHandleStub(TimerHandle_t timer,
                                 TimerCallbackBox* box) noexcept
    : timer_(timer), box_(box) {}

TimerHandleStub::TimerHandleStub(TimerHandleStub&& other) noexcept
    : timer_(std::exchange(other.timer_, nullptr)),
      box_(std::exchange(other.box_, nullptr)) {}

TimerHandleStub::~TimerHandleStub() {
    cancel();
}

TimerHandleStub& TimerHandleStub::operator=(TimerHandleStub&& other) noexcept {
    if (this != &other) {
        // Release this handle's timer before taking the other's.
        cancel();
        timer_ = std::exchange(other.timer_, nullptr);
        box_ = std::exchange(other.box_, nullptr);
    }
    return *this;
}

void TimerHandleStub::cancel() noexcept {
    if (timer_ == nullptr) {
        return;
    }

    // Both calls only queue commands for the timer service task, so neither can
    // guarantee that no callback is running when it returns; the contract
    // allows that. A timer that already expired is simply not armed any more,
    // which is not an error here.
    (void)xTimerStop(timer_, portMAX_DELAY);
    (void)xTimerDelete(timer_, portMAX_DELAY);

    // The timer itself is still alive until the service task drains the delete,
    // so the box it points at must outlive this call too: hand the free to the
    // same queue, behind the delete.
    // NOTE: hangs rather than fails if `cancel()` runs *inside* the timer
    // callback while the command queue is full (the only task that could drain
    // it is the one that is blocked). That is a pathological case for a test
    // stub; the queue holds `configTIMER_QUEUE_LENGTH` entries.
    if (xTimerPendFunctionCall(&free_callback_box, box_, 0, portMAX_DELAY) !=
        pdPASS) {
        // Unreachable with `portMAX_DELAY` unless the queue is wedged; leaking
        // is the only safe option left, so report it and move on rather than
        // freeing memory the timer task may still read.
        assert(false && "xTimerPendFunctionCall failed");
    }

    timer_ = nullptr;
    box_ = nullptr;
}

// --- OneShotTimerFactoryStub -------------------------------------------------

TimerHandleStub OneShotTimerFactoryStub::register_timer(
    std::chrono::milliseconds delay, std::function<void()> callback) {
    assert(callback != nullptr);
    TickType_t const ticks = delay_to_ticks(delay);

    auto* const box = new TimerCallbackBox(std::move(callback));

    // `pdFALSE`: one-shot, not auto-reload. The box is the timer's ID, which is
    // where `timer_callback()` reads it from.
    TimerHandle_t const timer =
        xTimerCreate("one-shot", ticks, pdFALSE, box, &timer_callback);
    if (timer == nullptr) {
        // No recovery is meaningful: out of memory here means the caller's wait
        // would never end.
        delete box;
        assert(false && "xTimerCreate failed");
        return TimerHandleStub();
    }

    if (xTimerStart(timer, portMAX_DELAY) != pdPASS) {
        // Do not leak the instance on a failed arm. The timer has no queued
        // callback yet (the start command is what would arm it), so the box
        // can be freed directly.
        (void)xTimerDelete(timer, portMAX_DELAY);
        delete box;
        assert(false && "xTimerStart failed");
        return TimerHandleStub();
    }

    return TimerHandleStub(timer, box);
}

}  // namespace thermo::hal
