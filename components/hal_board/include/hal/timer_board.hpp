// Hardware abstraction: one-shot timer, real `esp_timer` implementation for the
// development board.
//
// The shared timer vocabulary (`TimerHandleLike`, `OneShotTimerLike`) lives in
// the header-only `hal_common` component (`hal/timer_types.hpp`); this header
// adds the concrete `TimerHandleBoard` and `OneShotTimerBoard` classes.
// `hal_stub` provides the same two class names on top of FreeRTOS software
// timers (the `linux` target has no `esp_timer` implementation), so the code
// that must work with either takes the `hal::OneShotTimerLike` template
// parameter and never names a concrete timer.
//
// The implementation files are `hal/timer_board.hpp` / `timer_board.cpp` rather
// than the shared `hal/timer.hpp` name the other HAL devices use: there is no
// single `Timer` class for the two projects to share, only two class names, and
// naming the files after the implementation (`_board` here, `timer_stub` in
// `hal_stub`) makes it obvious which one a project linked.
//
// `esp_timer` dispatches the callback from the esp_timer task (the default
// `ESP_TIMER_TASK` method), which is what `OneShotTimerBoard` selects. The
// callback therefore runs in task context and may use the ordinary FreeRTOS
// APIs — but it must stay short, because that one task also dispatches every
// other `esp_timer` callback on the device.
//
// Ownership: `register_timer()` hands the caller a `TimerHandleBoard` that owns
// the `esp_timer` instance. Destroying the handle deletes the timer (cancelling
// it first), so a dropped handle frees its timer instead of leaking it. The
// handle is move-only for that reason: two owners would double-delete.
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

// A software-driven one-shot timer (`esp_timer`, task-dispatch).
class OneShotTimerBoard;

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
class TimerHandleBoard {
  public:
    TimerHandleBoard() noexcept = default;

    TimerHandleBoard(TimerHandleBoard const&) = delete;
    TimerHandleBoard(TimerHandleBoard&& other) noexcept;
    ~TimerHandleBoard();

    TimerHandleBoard& operator=(TimerHandleBoard const&) = delete;
    TimerHandleBoard& operator=(TimerHandleBoard&& other) noexcept;

    // Disarms the timer, so the callback will not be called afterwards.
    // Idempotent: cancelling a handle that holds no timer, or one whose timer
    // already expired, does nothing.
    void cancel() noexcept;

    // Whether this handle actually refers to a timer (a moved-from handle, or
    // one created empty, does not).
    [[nodiscard]] bool valid() const noexcept { return timer_ != nullptr; }

  private:
    friend class OneShotTimerBoard;

    // Takes ownership of an already-started `esp_timer` instance and of the
    // callback box its `arg` points at.
    TimerHandleBoard(esp_timer_handle_t timer,
                     std::shared_ptr<TimerCallbackBox> callback) noexcept;

    esp_timer_handle_t timer_ = nullptr;
    std::shared_ptr<TimerCallbackBox> callback_;
};

// Arms one-shot timers: after the requested delay each timer calls its callback
// once and then stays idle until re-armed.
class OneShotTimerBoard {
  public:
    OneShotTimerBoard() noexcept = default;

    OneShotTimerBoard(OneShotTimerBoard const&) = delete;
    // Movable so a board can own one and still be moved by value out of
    // `create()`. The factory holds no state — each registration owns its own
    // `esp_timer` — so moving one is indistinguishable from copying it.
    OneShotTimerBoard(OneShotTimerBoard&&) noexcept = default;
    ~OneShotTimerBoard() = default;
    OneShotTimerBoard& operator=(OneShotTimerBoard const&) = delete;
    OneShotTimerBoard& operator=(OneShotTimerBoard&&) noexcept = delete;

    // Schedules `callback` to run once after `delay`, returning the handle that
    // owns the timer. The callback takes no arguments and runs in the esp_timer
    // task; capture whatever state it needs in the callable.
    //
    // Fails loudly: an `esp_timer` that cannot be created or started (out of
    // memory, a delay of zero or less) trips an assert rather than silently
    // dropping the request. There is no failure path a caller could
    // meaningfully handle here, and a dropped timer would leave the caller
    // waiting forever.
    [[nodiscard]] TimerHandleBoard register_timer(
        std::chrono::milliseconds delay, std::function<void()> callback);
};

static_assert(TimerHandleLike<TimerHandleBoard>,
              "TimerHandleBoard must satisfy the TimerHandleLike concept");
static_assert(OneShotTimerLike<OneShotTimerBoard, TimerHandleBoard>,
              "OneShotTimerBoard must satisfy the OneShotTimerLike concept");

}  // namespace thermo::hal
