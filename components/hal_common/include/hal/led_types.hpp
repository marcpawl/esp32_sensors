// Shared, implementation-free types for an LED output (§2.1, §7).
//
// This header is the *single* definition of the LED vocabulary — the
// `LedState` enum and the `LedLike` concept — used by both LED
// implementations:
//
//   - `hal_board` — real GPIO implementation for the development board.
//   - `hal_stub`  — deterministic stub used by the host tests.
//
// Each implementation provides its own `hal/led.hpp` that includes this header
// and adds the concrete `Led` class, so there is exactly one definition of the
// shared types and the two implementations cannot drift. (A project still
// links exactly one of `hal_board` / `hal_stub`, but both `REQUIRES
// hal_common`.)
#pragma once

#include <concepts>
#include <cstdint>
#include <type_traits>

namespace thermo::hal {

// State of a single output. `kOff` is de-energized, `kOn` is energized.
enum class LedState : std::uint8_t {
    kOff = 0,
    kOn,
};

// Concept: an LED type that exposes the shared interface — an inline static
// `create(gpio, name)` factory, an `init()`, and a `write(state)` that is
// callable with an `LedState`. Both implementations satisfy it, so a project
// can `static_assert` which implementation it linked in.
//
// `name` is a `const char*` label; it is a human-readable identifier used by
// the stub for logging and ignored by the hardware implementation. It is
// passed as a pointer to a static string literal and is not owned.
template<typename T>
concept LedLike =
    std::is_class_v<T> && requires(int gpio, const char* name, LedState state) {
        { T::create(gpio, name) } -> std::same_as<T>;
        { std::declval<T&>().write(state) };
    };

}  // namespace thermo::hal
