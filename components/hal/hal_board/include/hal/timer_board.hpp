// Hardware abstraction: one-shot timer, real `esp_timer` implementation for the
// development board.
//
// The shared timer vocabulary (`TimerLike`, `OneShotTimerFactoryLike`)
// lives in the header-only `hal_common` component (`hal/timer_types.hpp`); this
// header adds the concrete `TimerBoard` and `OneShotTimerFactoryBoard`
// classes. `hal_stub` provides its own `TimerStub` /
// `OneShotTimerFactoryStub` pair on top of FreeRTOS software timers (the
// `linux` target has no `esp_timer` implementation), so the code that must work
// with either takes the `hal::OneShotTimerFactoryLike` template parameter and
// never names a concrete timer.
//
// The implementation files are `hal/timer_board.hpp` / `timer_board.cpp` rather
// than the shared `hal/timer.hpp` name the other HAL devices use: there is no
// single `Timer` class for the two projects to share, only two class names, and
// naming the files after the implementation (`_board` here, `timer_stub` in
// `hal_stub`) makes it obvious which one a project linked.
//
// `esp_timer` dispatches the callback from the esp_timer task (the default
// `ESP_TIMER_TASK` method), which is what `OneShotTimerFactoryBoard` selects.
// The callback therefore runs in task context and may use the ordinary FreeRTOS
// APIs — but it must stay short, because that one task also dispatches every
// other `esp_timer` callback on the device.
//
// Ownership: `register_timer()` hands the caller a `TimerBoard` that owns the
// `esp_timer` instance. Destroying the handle deletes the timer (cancelling it
// first), so a dropped handle frees its timer instead of leaking it. The handle
// is move-only for that reason: two owners would double-delete.
#pragma once

// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#include "esp_err.h"
#include "esp_timer.h"
#pragma GCC diagnostic pop
// NOLINTEND

#include <chrono>
#include <functional>
#include <utility>

#include "hal/timer_types.hpp"

namespace thermo::hal {

// The `esp_timer`-backed one-shot timer factory (forward declaration).
class OneShotTimerFactoryBoard;

// The callback storage the timer's `arg` points at. Held by the handle in a
// `shared_ptr` so it outlives every callback invocation.
using TimerCallbackBox = std::function<void()>;

// Owns one armed (or already expired) `esp_timer` instance.
//
// `cancel()` disarms the timer and waits for an in-flight callback to finish,
// so — unlike the abstract contract, which may only promise a racy cancel — a
// caller here can rely on `cancel()` returning only once no callback is
// running. `esp_timer` can only give that guarantee from task context, where
// this HAL is used; when `cancel()` is called from inside the very callback it
// is cancelling, the callback completes naturally after `cancel()` returns.
//
// The handle is move-only: it owns the timer and deletes it on destruction.
class TimerBoard {
  public:
    TimerBoard() noexcept = default;

    TimerBoard(TimerBoard const&) = delete;
    TimerBoard(TimerBoard&& other) noexcept;
    ~TimerBoard();

    TimerBoard& operator=(TimerBoard const&) = delete;
    TimerBoard& operator=(TimerBoard&& other) noexcept;

    // Disarms the timer, so the callback will not be called afterwards.
    // Idempotent: cancelling a handle that holds no timer, or one whose timer
    // already expired, does nothing.
    void cancel() noexcept;

    // Whether this handle actually refers to a timer (a moved-from handle, or
    // one created empty, does not).
    [[nodiscard]] bool valid() const noexcept { return timer_ != nullptr; }

  private:
    friend class OneShotTimerFactoryBoard;

    // Takes ownership of an already-started `esp_timer` instance and of the
    // callback box its `arg` points at.
    TimerBoard(esp_timer_handle_t timer,
               std::shared_ptr<TimerCallbackBox> callback) noexcept;

    esp_timer_handle_t timer_ = nullptr;
    std::shared_ptr<TimerCallbackBox> callback_;
};

// Arms one-shot timers: after the requested delay each timer calls its callback
// once and then stays idle. The factory itself holds no timer — each
// `register_timer()` creates its own `esp_timer` and hands it back through the
// returned handle.
class OneShotTimerFactoryBoard {
  public:
    OneShotTimerFactoryBoard() noexcept = default;

    OneShotTimerFactoryBoard(OneShotTimerFactoryBoard const&) = delete;
    // Movable so a board can own one and still be moved by value out of
    // `create()`. The factory holds no state — each registration owns its own
    // `esp_timer` — so moving one is indistinguishable from copying it.
    OneShotTimerFactoryBoard(OneShotTimerFactoryBoard&&) noexcept = default;
    ~OneShotTimerFactoryBoard() = default;
    OneShotTimerFactoryBoard& operator=(OneShotTimerFactoryBoard const&) =
        delete;
    OneShotTimerFactoryBoard& operator=(OneShotTimerFactoryBoard&&) noexcept =
        delete;

    // Schedules `callback` to run once after `delay`, returning the handle that
    // owns the timer. The callback takes no arguments and runs in the esp_timer
    // task; capture whatever state it needs in the callable.
    //
    // Fails loudly: an `esp_timer` that cannot be created or started (out of
    // memory, a delay of zero or less) trips an assert rather than silently
    // dropping the request. There is no failure path a caller could
    // meaningfully handle here, and a dropped timer would leave the caller
    // waiting forever.
    [[nodiscard]] TimerBoard register_timer(std::chrono::milliseconds delay,
                                            std::function<void()> callback);
};

static_assert(TimerLike<TimerBoard>,
              "TimerBoard must satisfy the TimerLike concept");
static_assert(
    OneShotTimerFactoryLike<OneShotTimerFactoryBoard, TimerBoard>,
    "OneShotTimerFactoryBoard must satisfy the OneShotTimerFactoryLike "
    "concept");

}  // namespace thermo::hal
