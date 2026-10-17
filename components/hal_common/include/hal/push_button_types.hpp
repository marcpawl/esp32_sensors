// Shared, implementation-free types for a push button input.
//
// This header is the *single* definition of the push-button vocabulary — the
// `ButtonState` enum and the `PushButtonLike` concept — used by both
// implementations:
//
//   - `hal_board` — real GPIO implementation for the development board.
//   - `hal_stub`  — deterministic stub used under the simulated environment and
//     host tests.
//
// Each implementation provides its own `hal/push_button.hpp` that includes
// this header and adds the concrete `PushButton` class, so there is exactly
// one definition of the shared types and the two implementations cannot drift.
// (A project still links exactly one of `hal_board` / `hal_stub`, but both
// `REQUIRES hal_common`.)
//
// The button is reported without semantics: it only exposes whether it is
// currently pressed (polled via `read()`) and can invoke a registered callback
// whenever that state changes (via `set_interrupt()`). Interpreting a press
// (edge detection, debouncing, hold timing) is the caller's job.
#pragma once

#include <concepts>
#include <cstdint>
#include <optional>
#include <type_traits>

namespace thermo::hal {

// State of a push button. `kOff` is released, `kOn` is pressed.
enum class ButtonState : std::uint8_t {
    kOff = 0,
    kOn,
};

// Handler invoked when the button's state changes. It receives the new state
// (`ButtonState::kOn` for a press, `ButtonState::kOff` for a release).
using ButtonCallback = void (*)(ButtonState new_state) noexcept;

// Registered interrupt handler, or `std::nullopt` when none is registered.
// Passing `std::nullopt` to `set_interrupt()` disables the callback, so
// nothing is invoked on a state change.
using ButtonHandler = std::optional<ButtonCallback>;

// Concept: a push-button type that exposes the shared interface — an inline
// static `create(gpio)` factory, a `read()` returning `ButtonState`, and a
// `set_interrupt()` that registers (or clears) a state-change callback.
// `hal_board` and `hal_stub` both satisfy it, so a project can `static_assert`
// which implementation it linked in.
template<typename T>
concept PushButtonLike =
    std::is_class_v<T> && requires(int gpio, ButtonHandler handler) {
        { T::create(gpio) } -> std::same_as<T>;
        { std::declval<T&>().read() } -> std::same_as<ButtonState>;
        { std::declval<T&>().set_interrupt(handler) } -> std::same_as<void>;
    };

}  // namespace thermo::hal
