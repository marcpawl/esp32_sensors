// Shared, implementation-free types for deep sleep (§3.8).
//
// This header is the *single* definition of the sleep vocabulary used by both
// board implementations:
//
//   - `hal_board` — calls the ESP-IDF deep-sleep API
//     (`esp_sleep_enable_timer_wakeup()` + `esp_deep_sleep_start()`) and
//     reports ESP-IDF's wake-cause bitmap.
//   - `hal_stub`  — records the request in software and reports the wake cause
//     a host test asked for, so the device's sleep/backoff decisions become
//     host-testable.
//
// Deep sleep is *not* a HAL device: there is no third sleep implementation to
// swap in, and the operation is a property of the whole board (it powers the
// board down) rather than of a peripheral. The vocabulary still lives here, in
// `hal_common`, because `BoardLike` (`hal/board_types.hpp`) names it and
// `hal_common` is where shared, implementation-free HAL types belong.
//
// The HAL is *semantics-free*: it knows how to enter deep sleep for a duration
// and how to report why the last wake happened, but not how long to sleep
// (§3.4 sampling interval, §3.7 backoff) or what a wake means — those are the
// caller's decisions.
//
// This header is deliberately dependency-free (only `<cstdint>`): it must
// compile for the ESP-IDF `linux` target, where `esp_sleep.h` is not
// self-contained (it includes `hal/gpio_types.h`, which is not on the include
// path of a project that links no GPIO driver).
#pragma once

#include <cstdint>

namespace thermo::hal {

// A wake source, named by its *bit position* in the wake-cause bitmap rather
// than by a bit mask, because that is how ESP-IDF models it
// (`esp_sleep_get_wakeup_causes()` returns `BIT(ESP_SLEEP_WAKEUP_*)`). The
// values therefore match `esp_sleep_source_t` exactly, so `hal_board` can pass
// ESP-IDF's bitmap straight through; `board.cpp` asserts that they still line
// up. Only the sources this firmware can actually wake from are listed.
enum class WakeCause : std::uint32_t {
    // The reset was not caused by a wake from sleep (i.e. a cold boot). This
    // is what `esp_sleep_get_wakeup_causes()` reports while running normally.
    kUndefined = 0,
    // Woken by the RTC timer, i.e. the `esp_sleep_enable_timer_wakeup()`
    // duration set up by `deep_sleep_start()` (§3.8).
    kTimer = 4,
};

// The wake-cause bitmap: one bit per `WakeCause`, in the bit position named by
// the enumerator. An empty bitmap means "no cause known"; ESP-IDF never
// produces one (a normal boot reports `kUndefined`).
using WakeCauses = std::uint32_t;

// The bit in a `WakeCauses` bitmap that represents `cause`.
[[nodiscard]] constexpr WakeCauses wake_cause_bit(WakeCause cause) noexcept {
    return static_cast<WakeCauses>(1u) << static_cast<std::uint32_t>(cause);
}

// Whether the last wake was caused (at least in part) by `cause`. Wake causes
// are a bitmap because several sources can fire at once.
[[nodiscard]] constexpr bool woke_from(WakeCauses causes,
                                       WakeCause cause) noexcept {
    return (causes & wake_cause_bit(cause)) != 0;
}

}  // namespace thermo::hal
