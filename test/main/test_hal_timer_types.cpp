// Tests for the timer HAL vocabulary (`hal/timer_types.hpp`).
//
// Step 1 of the timer HAL is the shared, implementation-free contract, so there
// is no device to drive yet: these tests pin the contract itself with small
// model types. The value is in the *rejections* — a concept that is too loose
// silently accepts an implementation whose handle cannot be cancelled, or whose
// `register_timer()` returns the wrong type, and the first real caller would
// then not compile (or worse, compile and leak a timer).
//
// The concrete implementations are tested where they live: the FreeRTOS
// software-timer stub by `test_hal_timer.cpp`, and the `esp_timer` board
// implementation by the firmware build (it has no `linux` target).
// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#pragma GCC diagnostic pop
// NOLINTEND

#include <chrono>
#include <functional>

#include "hal/timer_types.hpp"
#include "unity.h"

namespace {

using thermo::hal::OneShotTimerLike;
using thermo::hal::TimerHandleLike;

// --- Model types ------------------------------------------------------------

// The minimal conforming handle: a class with `cancel()`.
struct GoodHandle {
    void cancel() {}
};

// No `cancel()` at all.
struct NoCancelHandle {
    void stop() {}
};

// `cancel()` returns a value rather than nothing.
struct WrongReturnHandle {
    bool cancel() { return true; }
};

// A class with no timer interface whatsoever.
struct NotATimer {};

// The minimal conforming timer: `register_timer(ms, callback) -> handle`.
struct GoodTimer {
    GoodHandle register_timer(std::chrono::milliseconds /*delay*/,
                              std::function<void()> /*callback*/) {
        return {};
    }
};

// Returns a handle that does not satisfy `TimerHandleLike`, so it cannot model
// the timer whatever handle type the caller names.
struct NoCancelReturnTimer {
    NoCancelHandle register_timer(std::chrono::milliseconds /*delay*/,
                                  std::function<void()> /*callback*/) {
        return {};
    }
};

// Takes the wrong delay type, so `register_timer(ms, cb)` would not be
// callable.
struct WrongDelayTimer {
    GoodHandle register_timer(std::chrono::seconds /*delay*/,
                              std::function<void()> /*callback*/) {
        return {};
    }
};

// Takes the wrong callback type, so a `std::function<void()>` would not
// convert.
struct WrongCallbackTimer {
    GoodHandle register_timer(std::chrono::milliseconds /*delay*/,
                              void (* /*callback*/)()) {
        return {};
    }
};

// Returns by reference instead of by value: the caller would own a view on the
// timer's storage rather than a handle of its own.
struct ReferenceReturnTimer {
    GoodHandle& register_timer(std::chrono::milliseconds /*delay*/,
                               std::function<void()> /*callback*/) {
        static GoodHandle handle{};
        return handle;
    }
};

}  // namespace

// --- TimerHandleLike --------------------------------------------------------

TEST_CASE("hal timer types: a class with cancel() models TimerHandleLike",
          "[hal_timer_types]") {
    static_assert(TimerHandleLike<GoodHandle>);
    TEST_PASS();
}

TEST_CASE("hal timer types: TimerHandleLike requires cancel()",
          "[hal_timer_types]") {
    static_assert(!TimerHandleLike<NoCancelHandle>);
    static_assert(!TimerHandleLike<NotATimer>);
    // A non-class cannot be a handle, even if it had the right shape.
    static_assert(!TimerHandleLike<int>);
    // A reference is not a handle: the caller owns the handle by value.
    static_assert(!TimerHandleLike<GoodHandle&>);
    TEST_PASS();
}

TEST_CASE("hal timer types: TimerHandleLike requires a void cancel()",
          "[hal_timer_types]") {
    // The concept pins the return type: a `bool cancel()` would not satisfy it,
    // so an implementation cannot silently change what cancelling reports.
    static_assert(!TimerHandleLike<WrongReturnHandle>);
    TEST_PASS();
}

// --- OneShotTimerLike -------------------------------------------------------

TEST_CASE("hal timer types: a conforming timer models OneShotTimerLike",
          "[hal_timer_types]") {
    static_assert(OneShotTimerLike<GoodTimer, GoodHandle>);
    TEST_PASS();
}

TEST_CASE("hal timer types: OneShotTimerLike names its handle type",
          "[hal_timer_types]") {
    // The handle type is named by the caller rather than inferred from the
    // return type, so a conforming timer does not model the concept with some
    // *other* handle:
    static_assert(!OneShotTimerLike<GoodTimer, NoCancelHandle>);
    // ...and a timer whose return type is not a `TimerHandleLike` at all never
    // satisfies it, for any handle type.
    static_assert(!TimerHandleLike<NoCancelHandle>);
    static_assert(!OneShotTimerLike<NoCancelReturnTimer, GoodHandle>);
    static_assert(!OneShotTimerLike<NoCancelReturnTimer, NoCancelHandle>);
    TEST_PASS();
}

TEST_CASE("hal timer types: OneShotTimerLike pins the register_timer signature",
          "[hal_timer_types]") {
    // A `std::chrono::seconds` parameter cannot be called with a `milliseconds`
    // argument (that conversion loses resolution, so it is not implicit), so an
    // implementation cannot silently round the caller's delay away.
    static_assert(!OneShotTimerLike<WrongDelayTimer, GoodHandle>);
    // Likewise for the callback: a plain function-pointer parameter would
    // reject the capturing lambdas callers rely on.
    static_assert(!OneShotTimerLike<WrongCallbackTimer, GoodHandle>);
    // Returned by value, not by reference: the caller owns the handle.
    static_assert(!OneShotTimerLike<ReferenceReturnTimer, GoodHandle>);
    TEST_PASS();
}
