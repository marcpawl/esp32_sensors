// Shared, implementation-free types for a switch input.
//
// This header is the *single* definition of the switch vocabulary — the
// `SwitchState` enum and the `SwitchLike` concept — used by both
// implementations:
//
//   - `hal_board` — real GPIO implementation for the development board.
//   - `hal_stub`  — deterministic stub used by the host tests.
//
// Each implementation provides its own `hal_board/switch.hpp` (or
// `hal_stub/switch.hpp`) that includes
// this header and adds the concrete `Switch` class, so there is exactly
// one definition of the shared types and the two implementations cannot drift.
// (A project still links exactly one of `hal_board` / `hal_stub`, but both
// `REQUIRES hal_common`.)
//
// A switch is reported without semantics: it only exposes whether it is
// currently on (polled via `read()`), the pin it is wired to (`gpio()`), and a
// registered callback invoked whenever that state changes (via
// `set_interrupt()`). Interpreting an edge (debouncing, hold timing) is the
// caller's job.
#pragma once

// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#pragma GCC diagnostic pop
// NOLINTEND

#include <concepts>
#include <functional>
#include <type_traits>
#include <utility>

#include <cstdint>

#include "hal/gpio.hpp"
#include "utility/isr_safe.hpp"

namespace thermo::hal {

// State of a switch. `kOff` is open, `kOn` is closed.
enum class SwitchState : std::uint8_t {
    kOff = 0,
    kOn,
};

// Handler invoked when the switch's state changes. It takes no argument: the
// switch's new state is read through the switch itself (`Switch::read()`).
// This lets a handler capture the object it belongs to.
//
// The handler runs in the GPIO interrupt, so it must be ISR-safe: no blocking,
// no heap, no mutex. `set_interrupt()` annotates its parameter
// `[[thermo::isr_safe]]` to make that requirement explicit.
//
// The `noexcept` in the function type is part of the ISR contract, not
// decoration: a callback that might throw cannot be stored, so a lambda
// without `noexcept` refuses to convert. The wrapper is move-only because a
// registration has exactly one owner and is only ever moved into the switch.
//
// An empty callback means "no handler"; passing one to `set_interrupt()`
// unregisters the callback, so nothing is invoked on a state change.
using SwitchCallback = std::move_only_function<void() noexcept>;

// Concept: a switch type that exposes the shared interface — an inline
// static `create(Gpio)` factory, a `read()` returning `SwitchState`, a
// `gpio()` getter, a `set_interrupt()` that registers (or clears) a
// state-change callback, and a `remove_interrupt()` that unregisters it.
// `hal_board` and `hal_stub` both satisfy it, so a project can `static_assert`
// which implementation it linked in.
template<typename T>
concept SwitchLike =
    std::is_class_v<T> &&
    requires(Gpio gpio, [[thermo::isr_safe]] SwitchCallback callback) {
        { T::create(gpio) } -> std::same_as<T>;
        { std::declval<T const&>().gpio() } -> std::same_as<Gpio>;
        { std::declval<T&>().read() } -> std::same_as<SwitchState>;
        {
            std::declval<T&>().set_interrupt(std::move(callback))
        } -> std::same_as<void>;
        { std::declval<T&>().remove_interrupt() } -> std::same_as<void>;
    };

}  // namespace thermo::hal
