// LED status task: drives the LED indication from a message queue.
#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <variant>

#include "task.hpp"

namespace thermo {

// Operating states the LED task can display.
enum class LedState : std::uint8_t {
    kOff = 0,          // Power Off
    kBoot = 1,         // Booting
    kConfig = 2,       // Configure mode
    kConfig_save = 3,  // configuration being saved.
    kRun = 4,          // Run mode: sampling/uploading indication.
};

// Owns a FreeRTOS task that translates `LedState` messages into LED output.
// Other tasks publish state changes with `set_state()`; the LED work itself
// happens on this task.
class LedTask : public Task<LedTask> {
  public:
    // The queue element type consumed by this task.
    using Message = LedState;

    explicit LedTask(TaskParams params = {}) : Task<LedTask>(params) {}

    // Publishes a new LED state. Safe to call from any task; overwrites any
    // state not yet displayed.
    void set_state(LedState new_state) noexcept { send(new_state); }

    // Task body: waits for state messages and applies them. `[[noreturn]]`,
    // as required by `Task<Derived>`.
    [[noreturn]] void run() noexcept;

  private:
    enum class RedLed : std::int8_t { Off = 0, On };
    enum class GreenLed : std::int8_t { Off = 0, On };

    struct Step {
        std::chrono::milliseconds duration_;
        RedLed red_led_;
        GreenLed green_led_;

        // Any chrono duration (e.g. `std::chrono::hours{1}`, `500ms`) converts
        // implicitly to milliseconds. A non-template constructor keeps the type
        // usable in constant expressions declared earlier in this class.
        constexpr Step(std::chrono::milliseconds duration, RedLed red_led,
                       GreenLed green_led) noexcept
            : duration_(duration), red_led_(red_led), green_led_(green_led) {}
    };

    template<std::size_t N>
    struct Cycle {
        LedState state_;
        std::array<Step, N> steps_;

        static_assert(N > 0, "A cycle must contain at least one step.");

        template<typename... Args>
            requires(sizeof...(Args) == N)
        constexpr explicit(N == 1) Cycle(LedState state, Args&&... args)
            : state_(state), steps_{std::forward<Args>(args)...} {}
    };

    // Deduction guide to automatically deduce N from the number of passed steps
    template<typename... Args>
        requires(sizeof...(Args) > 0)
    Cycle(LedState state, Args...) -> Cycle<sizeof...(Args)>;

    // The compile-time table of LED cycles, indexed by `LedState`. Cycles have
    // differing step counts, so the element type is a variant.
    using Cycles = std::array<std::variant<Cycle<1>, Cycle<2>>, 5>;

    // The table, built entirely at compile time. Returned by value (the
    // constructing function is `constexpr`), which avoids referencing the
    // nested constructors from an in-class static member initializer.
    static constexpr Cycles cycles() noexcept {
        return {
            Cycle{LedState::kOff,
                  Step{std::chrono::hours{1}, RedLed::Off, GreenLed::Off}},

            Cycle{
                LedState::kBoot,
                Step{std::chrono::milliseconds{500}, RedLed::On, GreenLed::On},
                Step{std::chrono::milliseconds{500}, RedLed::Off,
                     GreenLed::Off}},

            Cycle{LedState::kConfig,
                  Step{std::chrono::hours{1}, RedLed::On, GreenLed::On}},

            Cycle{
                LedState::kConfig_save,
                Step{std::chrono::milliseconds{50}, RedLed::Off, GreenLed::Off},
                Step{std::chrono::milliseconds{50}, RedLed::On, GreenLed::On}},

            Cycle{LedState::kRun,
                  Step{std::chrono::hours{1}, RedLed::Off, GreenLed::On}},
        };
    }

    // Change the LEDs to the modes indicated.
    void change_leds(Step const& step) noexcept;

    // Execute until a new message comes in the cycle.
    // cycle: Description on how the LEDs will behave.
    // returns: New state that should be executed.
    template<std::size_t N>
    [[nodiscard]] LedState execute_cycle(const Cycle<N>& cycle) noexcept;

    // Applies a state change to the LEDs. Called on the LED task only.
    // Returns the next state when another state change has been
    // sent by set_state.
    [[nodiscard]] LedState on_state_changed(LedState new_state) noexcept;
};

}  // namespace thermo
