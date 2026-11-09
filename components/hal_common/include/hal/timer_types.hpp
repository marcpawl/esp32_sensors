// Shared, implementation-free types for a one-shot timer.
//
// This header is the *single* definition of the timer vocabulary — the
// `TimerHandleLike` and `OneShotTimerFactoryLike` concepts — used by both timer
// implementations:
//
//   - `hal_board` — real ESP-IDF timers (`esp_timer`), whose callback runs in
//     the timer task.
//   - `hal_stub`  — software-driven FreeRTOS software timers used by the host
//     tests.
//
// Each implementation provides its own header (`hal/timer_board.hpp` in
// `hal_board`, `hal/timer_stub.hpp` in `hal_stub`) that includes this one and
// adds the concrete classes, so there is exactly one definition of the shared
// contract and the two implementations cannot drift. (A project still links
// exactly one of `hal_board` / `hal_stub`, but both `REQUIRES hal_common`.)
//
// The HAL is *semantics-free* about when to arm a timer: it knows how to
// schedule a callback after a delay and how to cancel that schedule, but not
// how long the delay should be or what the callback does. Those are the
// caller's decisions.
//
// Cancellation is racy by nature, and the contract says so rather than
// pretending otherwise: `cancel()` stops the timer *associated with the
// handle*, but a callback that is already running (or already dispatched to the
// timer task) may still run to completion. A caller that must not act on a
// stale callback has to guard the callback body itself (e.g. against a
// generation counter or a cancelled flag); `cancel()` is not a barrier.
//
// This header is deliberately dependency-free of ESP-IDF: it names only
// standard types, so it compiles for the ESP-IDF `linux` target where no
// ESP-IDF timer driver exists.
#pragma once

#include <chrono>
#include <concepts>
#include <functional>
#include <type_traits>

namespace thermo::hal {

// Concept: the handle returned when a one-shot timer is armed.
//
// `cancel()` asks the timer not to run its callback. Because the callback may
// already be executing (or already be queued for the timer task) when
// `cancel()` is called, the callback must be prepared to run even after a
// successful cancel; see the file comment above. The handle adds nothing else
// to the contract — in particular it does not promise to report whether the
// callback ran.
template<typename T>
concept TimerHandleLike = std::is_class_v<T> && requires(T& handle) {
    { handle.cancel() } -> std::same_as<void>;
};

// Concept: a one-shot timer *factory* — a type that arms a callback to run once
// after a delay.
//
// `register_timer(delay, callback)` returns a handle for the armed timer: after
// `delay` elapses the timer calls `callback` exactly once, and the handle can
// be used to cancel it before it fires. "Factory" because that is all the type
// is: it holds no timer of its own, it creates one per call and hands it back
// through the handle.
//
// The timer does not own the caller's state, so a callback that needs data
// captures it (a lambda with a copy, or a reference to something that outlives
// the timer) — the HAL never sees that state.
//
// The returned handle's type is a template parameter rather than a fixed type
// so each implementation can return its own: `hal_board` and `hal_stub` arm
// timers through different mechanisms, and the caller only needs
// `TimerHandleLike` to cancel one.
template<typename T, typename TimerHandle>
concept OneShotTimerFactoryLike =
    std::is_class_v<T> && TimerHandleLike<TimerHandle> &&
    requires(T& timer, std::chrono::milliseconds delay,
             std::function<void()> callback) {
        { timer.register_timer(delay, callback) } -> std::same_as<TimerHandle>;
    };

}  // namespace thermo::hal
