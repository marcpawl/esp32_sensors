// Tests for FreeRTOS task primitives on the ESP-IDF `linux` target.
//
// `vTaskDelay()` is what `thermo::reboot()` uses to let serial output flush
// before it resets the chip, so it has to really block on the host as well as
// on the target. A delay that returned immediately would still build and link,
// which is exactly the failure this probe catches: it measures the call
// against both the scheduler tick and the wall clock.
//
// The bounds are deliberately loose. FreeRTOS aligns a delay to the tick, so
// the wait can be one tick shorter than the nominal duration (the tick in
// which `vTaskDelay()` was called is partly spent); half the nominal duration
// is a floor no working port can miss. The upper bound only catches an
// implementation that reads its argument as a wildly larger unit.
// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#pragma GCC diagnostic pop

// NOLINTEND

#include <chrono>

#include <cstdint>

#include "unity.h"

namespace {

// Long enough that the wall-clock check is meaningful (10 ticks at this
// project's 100 Hz tick rate) and short enough to keep the suite fast.
constexpr TickType_t kDelayTicks = pdMS_TO_TICKS(100);

// A delay can lose up to one tick to alignment, so half the nominal duration
// is a floor no working port can miss.
constexpr int kMinElapsedMs = 50;

// A `vTaskDelay()` that treats ticks as seconds (or worse) must fail rather
// than make the suite appear to pass after a long wait.
constexpr int kMaxElapsedMs = 5000;

}  // namespace

TEST_CASE("freertos task: vTaskDelay blocks for the requested time",
          "[freertos_task]") {
    static_assert(kDelayTicks > 0, "the delay must be at least one tick");

    const TickType_t ticks_before = xTaskGetTickCount();
    const auto start = std::chrono::steady_clock::now();

    vTaskDelay(kDelayTicks);

    const auto elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start)
            .count();
    const TickType_t ticks_after = xTaskGetTickCount();

    // The scheduler's tick advanced while this task was blocked, so the delay
    // really went through the kernel.
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(
        static_cast<std::uint32_t>(kDelayTicks),
        static_cast<std::uint32_t>(ticks_after - ticks_before));

    // ...and real time passed: a no-op returns in well under the floor.
    TEST_ASSERT_GREATER_OR_EQUAL_INT(kMinElapsedMs,
                                     static_cast<int>(elapsed_ms));
    TEST_ASSERT_LESS_OR_EQUAL_INT(kMaxElapsedMs, static_cast<int>(elapsed_ms));
}
