// Tests for the deep-sleep vocabulary (`hal/sleep_types.hpp`).
//
// The header is the shared, implementation-free contract `BoardLike` uses to
// report why the last wake happened. The hardware board passes ESP-IDF's
// wake-cause bitmap through unchanged, so the enumerator values must stay at
// ESP-IDF's bit positions and the helpers must read one bit at a time.
// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#pragma GCC diagnostic pop
// NOLINTEND

#include <cstdint>

#include "hal/sleep_types.hpp"
#include "unity.h"

namespace {

using thermo::hal::wake_cause_bit;
using thermo::hal::WakeCause;
using thermo::hal::woke_from;

}  // namespace

TEST_CASE("hal sleep types: the wake-cause vocabulary is a bit per wake source",
          "[hal_sleep_types]") {
    // One bit per source, in the position named by the enumerator, so the
    // hardware board can pass ESP-IDF's `BIT(ESP_SLEEP_WAKEUP_*)` bitmap
    // through unchanged.
    TEST_ASSERT_EQUAL_UINT32(1u << 0, wake_cause_bit(WakeCause::kUndefined));
    TEST_ASSERT_EQUAL_UINT32(1u << 4, wake_cause_bit(WakeCause::kTimer));
    // `kTimer` is ESP_SLEEP_WAKEUP_TIMER (4) in ESP-IDF, and `kUndefined` is
    // 0; `board.cpp` asserts the same thing on the hardware side.
    static_assert(static_cast<std::uint32_t>(WakeCause::kUndefined) == 0);
    static_assert(static_cast<std::uint32_t>(WakeCause::kTimer) == 4);
}

TEST_CASE("hal sleep types: woke_from() reads one source out of the bitmap",
          "[hal_sleep_types]") {
    // Wake causes are a bitmap because several sources can fire at once.
    const auto causes = wake_cause_bit(WakeCause::kTimer) |
                        wake_cause_bit(WakeCause::kUndefined);
    TEST_ASSERT_TRUE(woke_from(causes, WakeCause::kTimer));
    TEST_ASSERT_TRUE(woke_from(causes, WakeCause::kUndefined));
    TEST_ASSERT_TRUE(
        woke_from(wake_cause_bit(WakeCause::kTimer), WakeCause::kTimer));
    TEST_ASSERT_FALSE(
        woke_from(wake_cause_bit(WakeCause::kTimer), WakeCause::kUndefined));
    TEST_ASSERT_FALSE(woke_from(0, WakeCause::kTimer));
}
