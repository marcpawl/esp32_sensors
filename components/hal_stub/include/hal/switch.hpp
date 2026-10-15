// Hardware abstraction: 3-position ON-OFF-ON mode switch (§2.3).
//
// This header is the shared interface between the two switch components:
//   - `hal_board` — real GPIO implementation for the development board.
//   - `hal_stub`  — deterministic stub used under QEMU and host tests.
//
// A project depends on exactly one of them, and both expose this identical
// header (`hal/switch.hpp`) with the same `Switch` interface (including the
// inline `create()` factory and the `SwitchLike` concept), so application code
// includes it the same way regardless of which implementation is linked in.
//
// The switch is reported without semantics: the HAL only returns the raw state
// of the two sense pins. Mapping pin states to Configure / Off / Run is the
// caller's job.
//
// This stub replaces the GPIO-backed `hal_board` implementation under QEMU and
// in host tests, where the sense pins do not exist. The reported pin states are
// held in software and can be changed with `set_pins()`, so bad hardware
// (both pins on, or both pins off) can be simulated.
#pragma once

#include <concepts>
#include <cstdint>
#include <type_traits>

#include "esp_err.h"

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

// Reads a 3-position ON-OFF-ON switch wired to two sense pins.
class Switch {
  public:
    // `first_gpio` / `third_gpio` are the two sense pins (the outer poles).
    // The stub ignores them.
    Switch(int first_gpio, int third_gpio) noexcept;

    Switch(Switch const&) = delete;
    Switch(Switch&&) = delete;
    ~Switch() = default;
    Switch& operator=(Switch const&) = delete;
    Switch& operator=(Switch&&) = delete;

    // Builds a switch for the two given sense pins. Same signature for the
    // hardware and stub implementations; call init() before read().
    [[nodiscard]] static Switch create(int first_gpio,
                                       int third_gpio) noexcept {
        return Switch(first_gpio, third_gpio);
    }

    // No-op for the stub (there are no pins to configure); always succeeds.
    esp_err_t init() noexcept;

    // Returns the pin states set by `set_pins()`, defaulting to the Third
    // position (first off, third on).
    [[nodiscard]] Pins read() const noexcept;

    // Sets the pin states reported by subsequent `read()` calls. Intended for
    // tests and QEMU scenarios, including simulating bad hardware.
    void set_pins(Pins pins) noexcept;

  private:
    Pins pins_{PinState::kOff, PinState::kOn};
};

static_assert(SwitchLike<Switch>, "Switch must satisfy the SwitchLike concept");

}  // namespace thermo::hal
