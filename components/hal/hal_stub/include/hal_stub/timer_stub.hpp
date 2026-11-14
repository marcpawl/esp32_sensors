// Hardware abstraction: one-shot timer, software-driven stub for the host
// tests.
//
// The shared timer vocabulary (`TimerLike`, `OneShotTimerFactoryLike`)
// lives in the header-only `hal_common` component (`hal/timer_types.hpp`); this
// header adds the concrete `TimerStub` and `OneShotTimerFactoryStub`
// classes. `hal_board` provides the real implementation on top of `esp_timer`,
// so the code that must work with either takes the
// `hal::OneShotTimerFactoryLike` template parameter and never names a concrete
// timer.
//
// There is no `esp_timer` implementation on the `linux` target, so this stub is
// built on FreeRTOS software timers (`freertos/timers.h`) instead — the timer
// service task ("Tmr Svc") is what dispatches the callback, which
// `test/main/test_freertos_timers.cpp` pins as working on this target.
//
// The implementation files are `hal_stub/timer_stub.hpp` / `timer_stub.cpp`
// rather
// than the shared `hal/timer.hpp` name the other HAL devices use: there is no
// single `Timer` class for the two projects to share, only two class names, and
// naming the files after the implementation makes it obvious which one a
// project linked.
//
// Two properties of the FreeRTOS timer API shape this stub and are worth
// knowing before using it:
//
//   * Timer commands are **asynchronous**. `xTimerStart()`, `xTimerStop()` and
//     `xTimerDelete()` only queue a command for the timer service task, so a
//     timer may still report itself idle (or still fire) for a tick or so after
//     the call returns. A test that asserts on a timer must poll, exactly as
//     `test_freertos_timers.cpp` does.
//   * Cancellation is therefore **genuinely racy** here, unlike the `hal_board`
//     implementation, where `esp_timer_stop_blocking()` can promise that no
//     callback is running when `cancel()` returns. The abstract contract allows
//     that (see `hal/timer_types.hpp`), and the callback storage is arranged so
//     the race cannot read freed memory — but a caller that must not act on a
//     stale callback still has to guard the callback body itself.
//
// Ownership: `register_timer()` hands the caller a `TimerStub` that owns the
// FreeRTOS timer, so a dropped handle deletes the timer instead of leaking it.
// The handle is move-only for that reason: two owners would double-delete.
#pragma once

// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#pragma GCC diagnostic pop
// NOLINTEND

#include <chrono>
#include <functional>
#include <utility>

#include "hal/timer_types.hpp"

namespace thermo::hal::stub {

// The FreeRTOS-timer-backed one-shot timer factory (forward declaration).
class OneShotTimerFactoryStub;

// The callable the armed timer runs. Held by the timer itself (as its
// `pvTimerID`) and released once the timer has been deleted.
using TimerCallbackBox = std::function<void()>;

// Owns one armed (or already expired) FreeRTOS software timer.
//
// `cancel()` disarms and deletes the timer. Because the FreeRTOS calls are
// asynchronous, a callback that was already dispatched may still run after
// `cancel()` returns; the callback storage is kept alive until the timer
// service task has drained the delete, so that is safe but not instantaneous.
//
// The handle is move-only: it owns the timer and deletes it on destruction.
class TimerStub {
  public:
    TimerStub() noexcept = default;

    TimerStub(TimerStub const&) = delete;
    TimerStub(TimerStub&& other) noexcept;
    ~TimerStub();

    TimerStub& operator=(TimerStub const&) = delete;
    TimerStub& operator=(TimerStub&& other) noexcept;

    // Disarms the timer, so no *further* callback is scheduled, and releases
    // it. Idempotent: cancelling a handle that holds no timer, or one whose
    // timer already expired, does nothing.
    void cancel() noexcept;

    // Whether this handle actually refers to a timer (a moved-from handle, or
    // one created empty, does not).
    [[nodiscard]] bool valid() const noexcept { return timer_ != nullptr; }

  private:
    friend class OneShotTimerFactoryStub;

    // Takes ownership of an already-started timer and of its callback box.
    TimerStub(TimerHandle_t timer, TimerCallbackBox* box) noexcept;

    TimerHandle_t timer_ = nullptr;
    TimerCallbackBox* box_ = nullptr;
};

// Arms one-shot timers: after the requested delay each timer calls its callback
// once and then stays idle. The factory itself holds no timer — each
// `register_timer()` creates its own FreeRTOS timer and hands it back through
// the returned handle.
class OneShotTimerFactoryStub {
  public:
    OneShotTimerFactoryStub() noexcept = default;

    OneShotTimerFactoryStub(OneShotTimerFactoryStub const&) = delete;
    // Movable so a board can own one and still be moved by value out of
    // `create()`. The factory holds no state — each registration owns its own
    // FreeRTOS timer — so moving one is indistinguishable from copying it.
    OneShotTimerFactoryStub(OneShotTimerFactoryStub&&) noexcept = default;
    ~OneShotTimerFactoryStub() = default;
    OneShotTimerFactoryStub& operator=(OneShotTimerFactoryStub const&) = delete;
    OneShotTimerFactoryStub& operator=(OneShotTimerFactoryStub&&) noexcept =
        delete;

    // Schedules `callback` to run once after `delay`, returning the handle that
    // owns the timer. The callback takes no arguments and runs in the timer
    // service task; capture whatever state it needs in the callable.
    //
    // `delay` is rounded to whole ticks (`configTICK_RATE_HZ` is 100 on the
    // test target, so a tick is 10 ms). Fails loudly: a delay that is not at
    // least one tick, a null callback, or a failed create/start trips an assert
    // rather than silently dropping the request — a dropped timer would leave
    // the caller waiting forever.
    [[nodiscard]] TimerStub register_timer(std::chrono::milliseconds delay,
                                           std::function<void()> callback);
};

static_assert(TimerLike<TimerStub>,
              "TimerStub must satisfy the TimerLike concept");
static_assert(
    OneShotTimerFactoryLike<OneShotTimerFactoryStub, TimerStub>,
    "OneShotTimerFactoryStub must satisfy the OneShotTimerFactoryLike concept");

}  // namespace thermo::hal::stub
