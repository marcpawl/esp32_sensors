// Shared, implementation-free types for the 3-position ON-OFF-ON mode switch
// (§2.3).
//
// This header is the *single* definition of the switch vocabulary — the
// `PinState` enum, the `Pins` reading, and the `SwitchLike` concept — used by
// both switch implementations:
//
//   - `hal_board` — real GPIO implementation for the development board.
//   - `hal_stub`  — deterministic stub used by the host tests.
//
// Each implementation provides its own `hal/switch.hpp` that includes this
// header and adds the concrete `Switch` class, so there is exactly one
// definition of the shared types and the two implementations cannot drift.
// (A project still links exactly one of `hal_board` / `hal_stub`, but both
// `REQUIRES hal_common`.)
//
// The switch is reported without semantics: it only exposes the raw state of
// the two sense pins. Mapping pin states to Configure / Off / Run is the
// caller's job.
#pragma once

#include <concepts>
#include <cstdint>
#include <type_traits>

namespace thermo::hal {

// State of one physical switch pole.
enum class PinState : std::uint8_t {
    kOff = 0,
    kOn,
};

// The raw reading of the switch's two sense pins. Reporting both pins (rather
// than a single collapsed value) lets a caller detect bad hardware: a healthy
// 3-position switch energizes at most one pole at a time, so both-pins-on or
// both-pins-off are both observable (and distinguishable) failures.
//
//   | Position  | first      | third      |
//   |-----------|------------|------------|
//   | First     | kOn        | kOff       |
//   | Center    | kOff       | kOff       |
//   | Third     | kOff       | kOn        |
struct Pins {
    PinState first = PinState::kOff;
    PinState third = PinState::kOff;

    // Read the state of one pole as a bool (true == energized).
    [[nodiscard]] constexpr bool first_on() const noexcept {
        return first == PinState::kOn;
    }

    [[nodiscard]] constexpr bool third_on() const noexcept {
        return third == PinState::kOn;
    }

    friend constexpr bool operator==(Pins const&, Pins const&) = default;
};

// Concept: a switch type that exposes the shared interface — an inline static
// `create(first_gpio, third_gpio)` factory and a `read()` returning `Pins`.
// `hal_board` and `hal_stub` both satisfy it, so a project can `static_assert`
// which implementation it linked in.
template<typename T>
concept SwitchLike =
    std::is_class_v<T> && requires(int first_gpio, int third_gpio) {
        { T::create(first_gpio, third_gpio) } -> std::same_as<T>;
        { std::declval<T const&>().read() } -> std::same_as<Pins>;
    };

}  // namespace thermo::hal
