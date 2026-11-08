// Tests for FreeRTOS one-shot software timers (`freertos/timers.h`) on the
// ESP-IDF `linux` target.
//
// The firmware needs a one-shot timer (a deferred "this state has been held
// long enough" step, and any future deferred work) driven by the FreeRTOS timer
// service task rather than by the sample loop. These tests pin the behaviour we
// rely on: a timer built with `xTimerCreate(..., pdFALSE, ...)` starts idle,
// fires its callback exactly once after the period, goes idle again, can be
// re-armed, and does not fire at all when it is stopped first.
//
// ESP-IDF's `linux` FreeRTOS port runs the scheduler over POSIX threads with a
// SIGALRM-driven tick (100 Hz in this project's test sdkconfig), so a real
// wall-clock wait is the only way to observe a timer expire. Two things follow:
//
//   * Every wait is bounded, so a timer that never fires fails the test instead
//     of hanging it.
//   * Timer commands are *asynchronous*: `xTimerStart()` only queues a command
//     for the "Tmr Svc" task, so the timer may still report inactive when the
//     call returns. The active/inactive assertions therefore poll with a
//     bounded wait (`wait_for_active`) instead of reading the flag once.
//
// Each test owns its own counter (passed to the timer as its ID, the intended
// use of that parameter), so a timer left behind by a failing test cannot make
// a later one pass.
// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#pragma GCC diagnostic pop

// NOLINTEND

#include <array>
#include <atomic>

#include <cstddef>

#include "unity.h"

namespace {

// Static duration and one element per test: the callback needs storage that
// outlives a test that aborted on a failed assertion.
constexpr std::size_t kCounterCount = 4;
std::array<std::atomic<int>, kCounterCount> g_calls{};

// Long enough to be polled while armed (the port's tick is 100 Hz, so this is
// 20 ticks) and short enough to keep the suite fast.
constexpr TickType_t kPeriodTicks = pdMS_TO_TICKS(200);

// The step and bound for every bounded wait.
constexpr TickType_t kPollStep = pdMS_TO_TICKS(20);
constexpr TickType_t kWaitLimit = pdMS_TO_TICKS(4000);

// Long enough for many periods to elapse, so "it fired only once" is a
// meaningful observation rather than a race with the first expiry.
constexpr TickType_t kQuietTicks = kPeriodTicks * 10;

// The timer's ID is the counter it must bump.
void count_call(TimerHandle_t timer) noexcept {
    auto* counter = static_cast<std::atomic<int>*>(pvTimerGetTimerID(timer));
    counter->fetch_add(1);
}

// Creates a one-shot timer whose callback increments `calls`. The period is a
// full one-shot period; `pdFALSE` is the "no auto-reload" flag under test.
TimerHandle_t make_one_shot(std::atomic<int>& calls) {
    return xTimerCreate("one-shot", kPeriodTicks, pdFALSE, &calls, count_call);
}

// Bounded wait, in poll steps, for the callback to have run `expected` times.
// Returns the observed count so the caller can assert on it.
int wait_for_calls(std::atomic<int>& calls, int expected) {
    for (TickType_t waited = 0; calls.load() < expected && waited < kWaitLimit;
         waited += kPollStep) {
        vTaskDelay(kPollStep);
    }
    return calls.load();
}

// Bounded wait for the timer to report the given active state. Returns whether
// it was observed.
bool wait_for_active(TimerHandle_t timer, BaseType_t expected_active) {
    for (TickType_t waited = 0; waited < kWaitLimit; waited += kPollStep) {
        if (xTimerIsTimerActive(timer) == expected_active) {
            return true;
        }
        vTaskDelay(kPollStep);
    }
    return xTimerIsTimerActive(timer) == expected_active;
}

// Gives the timer several periods to misbehave, then returns the count.
int calls_after_quiet_period(std::atomic<int>& calls) {
    vTaskDelay(kQuietTicks);
    return calls.load();
}

}  // namespace

// --- Creation ---------------------------------------------------------------

TEST_CASE("freertos timers: a one-shot timer is created idle",
          "[freertos_timers]") {
    // A period below one tick rounds down to zero, which FreeRTOS rejects.
    static_assert(kPeriodTicks > 0, "the period must be at least one tick");

    g_calls[0].store(0);
    TimerHandle_t timer = make_one_shot(g_calls[0]);
    // `xTimerCreate` returns NULL when configUSE_TIMERS is off, which would
    // make every later assertion here vacuous.
    TEST_ASSERT_NOT_NULL(timer);

    // A newly created timer is not armed and has not called back. No command
    // has been queued, so this is not racy.
    TEST_ASSERT_FALSE(xTimerIsTimerActive(timer));
    TEST_ASSERT_EQUAL_INT(0, calls_after_quiet_period(g_calls[0]));

    TEST_ASSERT_EQUAL(pdPASS, xTimerDelete(timer, portMAX_DELAY));
}

// --- Firing exactly once ----------------------------------------------------

TEST_CASE("freertos timers: a one-shot timer calls back exactly once",
          "[freertos_timers]") {
    g_calls[1].store(0);
    TimerHandle_t timer = make_one_shot(g_calls[1]);
    TEST_ASSERT_NOT_NULL(timer);

    TEST_ASSERT_EQUAL(pdPASS, xTimerStart(timer, portMAX_DELAY));
    // The start is queued for the timer service task, so poll for "armed".
    TEST_ASSERT_TRUE(wait_for_active(timer, pdTRUE));

    // Fires after the period...
    TEST_ASSERT_EQUAL_INT(1, wait_for_calls(g_calls[1], 1));

    // ...and only once: `pdFALSE` means no auto-reload, so many further
    // periods must not produce a second call.
    TEST_ASSERT_EQUAL_INT(1, calls_after_quiet_period(g_calls[1]));

    // An expired one-shot timer is inactive again.
    TEST_ASSERT_TRUE(wait_for_active(timer, pdFALSE));

    TEST_ASSERT_EQUAL(pdPASS, xTimerDelete(timer, portMAX_DELAY));
}

// --- Re-arming --------------------------------------------------------------

TEST_CASE("freertos timers: a one-shot timer can be re-armed",
          "[freertos_timers]") {
    g_calls[2].store(0);
    TimerHandle_t timer = make_one_shot(g_calls[2]);
    TEST_ASSERT_NOT_NULL(timer);

    TEST_ASSERT_EQUAL(pdPASS, xTimerStart(timer, portMAX_DELAY));
    TEST_ASSERT_EQUAL_INT(1, wait_for_calls(g_calls[2], 1));
    TEST_ASSERT_TRUE(wait_for_active(timer, pdFALSE));

    // Starting an expired one-shot timer arms it for exactly one more period.
    TEST_ASSERT_EQUAL(pdPASS, xTimerStart(timer, portMAX_DELAY));
    TEST_ASSERT_TRUE(wait_for_active(timer, pdTRUE));
    TEST_ASSERT_EQUAL_INT(2, wait_for_calls(g_calls[2], 2));

    // ...and it stays one-shot on every arm, not just the first.
    TEST_ASSERT_EQUAL_INT(2, calls_after_quiet_period(g_calls[2]));
    TEST_ASSERT_TRUE(wait_for_active(timer, pdFALSE));

    TEST_ASSERT_EQUAL(pdPASS, xTimerDelete(timer, portMAX_DELAY));
}

// --- Stopping ---------------------------------------------------------------

TEST_CASE("freertos timers: stopping a one-shot timer cancels the callback",
          "[freertos_timers]") {
    g_calls[3].store(0);
    TimerHandle_t timer = make_one_shot(g_calls[3]);
    TEST_ASSERT_NOT_NULL(timer);

    TEST_ASSERT_EQUAL(pdPASS, xTimerStart(timer, portMAX_DELAY));
    TEST_ASSERT_TRUE(wait_for_active(timer, pdTRUE));

    // Stopped before the first expiry, so the callback never runs, however long
    // we wait afterwards.
    TEST_ASSERT_EQUAL(pdPASS, xTimerStop(timer, portMAX_DELAY));
    TEST_ASSERT_TRUE(wait_for_active(timer, pdFALSE));
    TEST_ASSERT_EQUAL_INT(0, calls_after_quiet_period(g_calls[3]));

    TEST_ASSERT_EQUAL(pdPASS, xTimerDelete(timer, portMAX_DELAY));
}
