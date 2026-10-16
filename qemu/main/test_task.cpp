// QEMU on-target tests for `thermo::Task` (`espidf_cpp`).
//
// `Task<Runner>` creates a real FreeRTOS task and runs `Runner::run()` on it,
// so it cannot be host-tested on the `linux` target: it needs the real
// FreeRTOS scheduler, which the ESP32-S3 QEMU image provides. These tests
// therefore live here rather than in `test/`.
//
// Design notes:
//   * `Runner::run()` must be `[[noreturn]]`, so each runner hands its result
//     to the test and then blocks forever. A `Task` has no stop API, and the
//     QEMU run is short-lived, so the task is simply left running.
//   * A `Task` is non-movable and non-copyable, so it is created in place in a
//     file-scope `std::optional` (one per test, reset between cases).
//   * The runner signals the test via a FreeRTOS task notification directed at
//     the test task, so the test waits with a bounded timeout instead of
//     polling or sleeping a fixed time.
#include "unity.h"

#include <cstdint>
#include <optional>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "task.hpp"

namespace {

// How long a test waits for its runner before failing. Generous: the suite
// runs natively but scheduling is not instantaneous.
constexpr TickType_t kWaitTicks = pdMS_TO_TICKS(2000);

// Shared hand-off between a runner (on the new task) and the test (on the
// entry task). The runner writes `value`, `ran_on` and then notifies
// `observer`.
struct Signal {
    TaskHandle_t observer = nullptr;
    TaskHandle_t ran_on = nullptr;
    std::uint32_t value = 0;
};

// Runner that records which task it ran on and signals the test, then blocks
// forever. `run()` is `[[noreturn]]`, as `Task` requires.
struct SignallingRunner {
    Signal* signal = nullptr;

    [[noreturn]] void run() noexcept {
        signal->ran_on = xTaskGetCurrentTaskHandle();
        signal->value = 0x5AA5;
        xTaskNotifyGive(signal->observer);
        for (;;) {
            vTaskDelay(portMAX_DELAY);
        }
    }
};

// Runner that blocks until the test notifies the Task's handle, records the
// received value, and signals back. Used to prove `handle()` addresses the
// running task.
struct NotifyRunner {
    Signal* signal = nullptr;

    [[noreturn]] void run() noexcept {
        signal->ran_on = xTaskGetCurrentTaskHandle();
        // Tell the test we are ready and waiting for its notification.
        xTaskNotifyGive(signal->observer);
        const std::uint32_t received = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        signal->value = received;
        xTaskNotifyGive(signal->observer);
        for (;;) {
            vTaskDelay(portMAX_DELAY);
        }
    }
};

// Per-test storage: a `Task` must be created in place and outlive the runner.
// Each `TEST_CASE` resets the one it uses before returning.
Signal g_signal;
std::optional<thermo::Task<SignallingRunner>> g_signalling_task;
std::optional<thermo::Task<NotifyRunner>> g_notify_task;

}  // namespace

// --- Construction / handle / name -------------------------------------------

TEST_CASE("qemu task: constructor creates a running task with a handle",
          "[qemu][task]") {
    g_signal = Signal{.observer = xTaskGetCurrentTaskHandle()};
    g_signalling_task.emplace(SignallingRunner{&g_signal},
                              thermo::TaskParams{.name = "sigtask"});

    // The handle is valid, the name was copied into the Task's own storage, and
    // the body ran (the runner notifies us).
    TEST_ASSERT_NOT_NULL(g_signalling_task->handle());
    TEST_ASSERT_EQUAL_STRING("sigtask", g_signalling_task->name());

    TEST_ASSERT_EQUAL_UINT32(1, ulTaskNotifyTake(pdTRUE, kWaitTicks));
    TEST_ASSERT_EQUAL_UINT32(0x5AA5, g_signal.value);

    g_signalling_task.reset();
}

TEST_CASE("qemu task: the body runs on a different task than the caller",
          "[qemu][task]") {
    const TaskHandle_t caller = xTaskGetCurrentTaskHandle();
    g_signal = Signal{.observer = caller};
    g_signalling_task.emplace(SignallingRunner{&g_signal},
                              thermo::TaskParams{.name = "other"});

    TEST_ASSERT_EQUAL_UINT32(1, ulTaskNotifyTake(pdTRUE, kWaitTicks));

    // `Runner::run()` executed on the new task, not on the caller's.
    TEST_ASSERT_NOT_NULL(g_signal.ran_on);
    TEST_ASSERT_TRUE(g_signal.ran_on != caller);
    // The runner ran on the very task whose handle the Task exposes.
    TEST_ASSERT_TRUE(g_signal.ran_on == g_signalling_task->handle());

    g_signalling_task.reset();
}

TEST_CASE("qemu task: default name is used when TaskParams is defaulted",
          "[qemu][task]") {
    g_signal = Signal{.observer = xTaskGetCurrentTaskHandle()};
    g_signalling_task.emplace(SignallingRunner{&g_signal},
                              thermo::TaskParams{});

    TEST_ASSERT_NOT_NULL(g_signalling_task->handle());
    // TaskParams defaults `name` to "task".
    TEST_ASSERT_EQUAL_STRING("task", g_signalling_task->name());

    TEST_ASSERT_EQUAL_UINT32(1, ulTaskNotifyTake(pdTRUE, kWaitTicks));

    g_signalling_task.reset();
}

TEST_CASE("qemu task: the task name is copied, not aliased", "[qemu][task]") {
    g_signal = Signal{.observer = xTaskGetCurrentTaskHandle()};

    // A stack buffer that goes out of scope after construction: the Task must
    // have copied the name into its own inline storage, so `name()` stays
    // valid (and correct) afterwards.
    {
        char buf[] = "copied";
        g_signalling_task.emplace(SignallingRunner{&g_signal},
                                  thermo::TaskParams{.name = buf});
        buf[0] = 'X';  // mutate the source; the Task's copy must not change
    }

    TEST_ASSERT_EQUAL_STRING("copied", g_signalling_task->name());

    TEST_ASSERT_EQUAL_UINT32(1, ulTaskNotifyTake(pdTRUE, kWaitTicks));

    g_signalling_task.reset();
}

TEST_CASE("qemu task: handle() addresses the running task", "[qemu][task]") {
    g_signal = Signal{.observer = xTaskGetCurrentTaskHandle()};
    g_notify_task.emplace(NotifyRunner{&g_signal},
                          thermo::TaskParams{.name = "notify"});

    // Wait until the runner is blocked on its notification.
    TEST_ASSERT_EQUAL_UINT32(1, ulTaskNotifyTake(pdTRUE, kWaitTicks));

    // Notify the running task through the exposed handle; the runner records
    // the value and notifies us back, proving the handle addressed it.
    xTaskNotifyGive(g_notify_task->handle());
    TEST_ASSERT_EQUAL_UINT32(1, ulTaskNotifyTake(pdTRUE, kWaitTicks));
    TEST_ASSERT_EQUAL_UINT32(1, g_signal.value);

    g_notify_task.reset();
}
