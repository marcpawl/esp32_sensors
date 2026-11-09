// Tests for the stub one-shot timer (`hal_stub`, `hal/timer_stub.hpp`).
//
// The stub is built on FreeRTOS software timers, so these tests run on the
// ESP-IDF `linux` target with Unity and a live scheduler: the callback really
// is dispatched by the timer service task, and a real wall-clock wait is the
// only way to observe it. Two consequences, the same ones
// `test_freertos_timers.cpp` documents:
//
//   * Every wait is bounded, so a timer that never fires fails the test instead
//     of hanging it.
//   * `xTimerStart()` / `xTimerStop()` / `xTimerDelete()` only *queue* a
//     command for the timer service task, so a timer may still be armed (or
//     still fire) for a tick after the call returns. Assertions about "it did
//     not fire" therefore wait out several periods rather than reading once.
//
// Every counter the callbacks write to is a function-local `static`: a callback
// can outlive the test that armed it (cancellation is racy by contract), so it
// must never write to a stack local — and a static has the lifetime for it
// while keeping each test's counter private to that test. Because the callbacks
// name those statics directly, their lambdas capture nothing, which is also
// what `clang-tidy`'s unused-lambda-capture check requires.
// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#pragma GCC diagnostic pop
// NOLINTEND

#include <atomic>
#include <chrono>
#include <concepts>
#include <functional>
#include <utility>

#include "hal/timer_stub.hpp"
#include "hal/timer_types.hpp"
#include "unity.h"

namespace {

using thermo::hal::OneShotTimerLike;
using thermo::hal::OneShotTimerStub;
using thermo::hal::TimerHandleLike;
using thermo::hal::TimerHandleStub;

// Long enough to be cancelled before it expires, short enough to keep the suite
// fast (`configTICK_RATE_HZ` is 100 on this target, so this is 20 ticks).
constexpr std::chrono::milliseconds kDelay{200};

// The step and bound for every bounded wait.
constexpr std::chrono::milliseconds kPollStep{20};
constexpr std::chrono::milliseconds kWaitLimit{4000};

// Long enough for many periods to elapse, so "it fired only once" (or "it never
// fired") is a meaningful observation rather than a race with the first expiry.
constexpr std::chrono::milliseconds kQuiet = kDelay * 10;

// `TickType_t` explicitly: on this port `pdMS_TO_TICKS()` is not an lvalue of
// an assignable type, so a loop counter cannot be deduced from it.
constexpr TickType_t kPollTicks = kPollStep.count() * configTICK_RATE_HZ / 1000;
constexpr TickType_t kLimitTicks =
    kWaitLimit.count() * configTICK_RATE_HZ / 1000;
constexpr TickType_t kQuietTicks = kQuiet.count() * configTICK_RATE_HZ / 1000;

// Bounded wait for the callback to have run `expected` times. Returns the
// observed count so the caller can assert on it.
int wait_for_calls(std::atomic<int>& calls, int expected) {
    for (TickType_t waited = 0; calls.load() < expected && waited < kLimitTicks;
         waited += kPollTicks) {
        vTaskDelay(kPollTicks);
    }
    return calls.load();
}

// Bounded wait for an observable value to become non-zero.
int wait_for_nonzero(std::atomic<int>& observed) {
    for (TickType_t waited = 0; observed.load() == 0 && waited < kLimitTicks;
         waited += kPollTicks) {
        vTaskDelay(kPollTicks);
    }
    return observed.load();
}

// Gives the timer several periods to misbehave, then returns the count.
int calls_after_quiet_period(std::atomic<int>& calls) {
    vTaskDelay(kQuietTicks);
    return calls.load();
}

}  // namespace

// --- Concept / aliasing -----------------------------------------------------

TEST_CASE("hal timer stub: the stub models both timer concepts",
          "[hal_timer_stub]") {
    static_assert(TimerHandleLike<TimerHandleStub>);
    static_assert(OneShotTimerLike<OneShotTimerStub, TimerHandleStub>);
    // `register_timer()` returns the stub's own handle by value, so the caller
    // owns it.
    static_assert(
        std::same_as<decltype(std::declval<OneShotTimerStub&>().register_timer(
                         kDelay, std::function<void()>{})),
                     TimerHandleStub>);
    TEST_PASS();
}

TEST_CASE("hal timer stub: a default handle is invalid and cancel is a no-op",
          "[hal_timer_stub]") {
    TimerHandleStub handle;
    TEST_ASSERT_FALSE(handle.valid());
    // Must not assert, leak, or touch the timer service task.
    handle.cancel();
    TEST_ASSERT_FALSE(handle.valid());
}

// --- Firing ----------------------------------------------------------------

TEST_CASE("hal timer stub: register_timer fires the callback exactly once",
          "[hal_timer_stub]") {
    static std::atomic<int> calls;
    calls.store(0);

    OneShotTimerStub timer;
    TimerHandleStub const handle =
        timer.register_timer(kDelay, [] { calls.fetch_add(1); });
    TEST_ASSERT_TRUE(handle.valid());

    // Fires after the delay...
    TEST_ASSERT_EQUAL_INT(1, wait_for_calls(calls, 1));
    // ...and only once: many further periods must not produce a second call.
    TEST_ASSERT_EQUAL_INT(1, calls_after_quiet_period(calls));
}

TEST_CASE("hal timer stub: the callback runs the state the caller captured",
          "[hal_timer_stub]") {
    // A by-value capture is the interesting case: it proves the callable is
    // really moved into the timer and run there, not just counted. `payload` is
    // a non-const local, so the lambda cannot name it without capturing it.
    static std::atomic<int> observed;
    observed.store(0);
    int payload = 4242;

    OneShotTimerStub timer;
    TimerHandleStub const handle =
        timer.register_timer(kDelay, [payload] { observed.store(payload); });

    // The callback stores the captured value rather than incrementing, so wait
    // for it directly.
    TEST_ASSERT_EQUAL_INT(payload, wait_for_nonzero(observed));
    TEST_ASSERT_TRUE(handle.valid());
}

// --- Cancelling ------------------------------------------------------------

TEST_CASE("hal timer stub: cancel() before the expiry prevents the callback",
          "[hal_timer_stub]") {
    static std::atomic<int> calls;
    calls.store(0);

    OneShotTimerStub timer;
    TimerHandleStub handle =
        timer.register_timer(kDelay, [] { calls.fetch_add(1); });
    TEST_ASSERT_TRUE(handle.valid());

    // Cancelled while it is still armed, so the callback never runs, however
    // long we wait afterwards.
    handle.cancel();
    TEST_ASSERT_FALSE(handle.valid());
    TEST_ASSERT_EQUAL_INT(0, calls_after_quiet_period(calls));

    // Cancelling again is harmless.
    handle.cancel();
    TEST_ASSERT_EQUAL_INT(0, calls.load());
}

TEST_CASE("hal timer stub: cancelling after the expiry is harmless",
          "[hal_timer_stub]") {
    static std::atomic<int> calls;
    calls.store(0);

    OneShotTimerStub timer;
    TimerHandleStub handle =
        timer.register_timer(kDelay, [] { calls.fetch_add(1); });

    // Let it fire first; the timer is then idle, and cancelling an idle timer
    // must still release it without a second callback.
    TEST_ASSERT_EQUAL_INT(1, wait_for_calls(calls, 1));
    handle.cancel();
    TEST_ASSERT_FALSE(handle.valid());
    TEST_ASSERT_EQUAL_INT(1, calls_after_quiet_period(calls));
}

// --- Ownership -------------------------------------------------------------

TEST_CASE("hal timer stub: move construction transfers the timer",
          "[hal_timer_stub]") {
    static std::atomic<int> calls;
    calls.store(0);

    OneShotTimerStub timer;
    TimerHandleStub handle =
        timer.register_timer(kDelay, [] { calls.fetch_add(1); });
    TEST_ASSERT_TRUE(handle.valid());

    TimerHandleStub moved = std::move(handle);
    TEST_ASSERT_FALSE(handle.valid());
    TEST_ASSERT_TRUE(moved.valid());

    // The moved-from handle owns nothing, so cancelling it must not cancel (or
    // double-delete) the timer the other handle now owns.
    handle.cancel();
    TEST_ASSERT_EQUAL_INT(1, wait_for_calls(calls, 1));
}

TEST_CASE("hal timer stub: move assignment releases the previous timer",
          "[hal_timer_stub]") {
    static std::atomic<int> replaced;
    static std::atomic<int> taken;
    replaced.store(0);
    taken.store(0);

    OneShotTimerStub timer;
    TimerHandleStub target =
        timer.register_timer(kDelay, [] { replaced.fetch_add(1); });
    TimerHandleStub source =
        timer.register_timer(kDelay, [] { taken.fetch_add(1); });

    // Assigning over `target` cancels the timer it owned, so only the timer
    // from `source` survives.
    target = std::move(source);
    TEST_ASSERT_TRUE(target.valid());
    TEST_ASSERT_FALSE(source.valid());

    TEST_ASSERT_EQUAL_INT(1, wait_for_calls(taken, 1));
    TEST_ASSERT_EQUAL_INT(0, calls_after_quiet_period(replaced));
}

// --- Independence -----------------------------------------------------------

TEST_CASE("hal timer stub: each registration arms an independent timer",
          "[hal_timer_stub]") {
    // Two handles writing to one counter must still produce exactly two calls,
    // so a shared counter is enough to prove both timers were armed.
    static std::atomic<int> calls;
    calls.store(0);

    OneShotTimerStub timer;
    TimerHandleStub a =
        timer.register_timer(kDelay, [] { calls.fetch_add(1); });
    TimerHandleStub b =
        timer.register_timer(kDelay, [] { calls.fetch_add(1); });

    TEST_ASSERT_TRUE(a.valid());
    TEST_ASSERT_TRUE(b.valid());
    TEST_ASSERT_EQUAL_INT(2, wait_for_calls(calls, 2));
    TEST_ASSERT_EQUAL_INT(2, calls_after_quiet_period(calls));
}
