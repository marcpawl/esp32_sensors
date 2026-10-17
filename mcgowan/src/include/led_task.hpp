// LED status task: drives the LED indication from a message queue.
#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <variant>

#include "espidf_cpp/one_message_queue.hpp"
#include "espidf_cpp/task.hpp"

namespace thermo {
// Operating states the LED task can display.
enum class LedState : std::uint8_t {
    kOff = 0,       // Power Off
    kBoot,          // Booting
    kConfig,        // Configure mode
    kConfig_save,   // configuration being saved.
    kConfig_reset,  // Configuration is to be reset to empty.
    kRun,           // Run mode: sampling/uploading indication.
    kRun_battery_low,
    kRun_uploading,
    kRun_upload_error,
};

/** Turns on and off the LEDs.
 */
class LedRunner {
  public:
    LedRunner() = default;

    LedRunner(LedRunner const&) = delete;

    LedRunner(LedRunner&&) = default;

    ~LedRunner() = default;

    LedRunner& operator=(LedRunner const&) = delete;

    LedRunner& operator=(LedRunner&&) = delete;

    // Publishes a new LED state. Safe to call from any task; overwrites any
    // state not yet displayed.
    void set_state(LedState new_state) noexcept { send(new_state); }

    // Task body: waits for state messages and applies them. `[[noreturn]]`,
    // as required by `espidf_cpp::Task<Derived>`.
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
    struct CycleSteps {
        LedState state_;
        std::array<Step, N> steps_;

        static_assert(N > 0, "A cycle must contain at least one step.");

        template<typename... Args>
            requires(sizeof...(Args) == N)
        constexpr explicit(N == 1) CycleSteps(LedState state, Args&&... args)
            : state_(state), steps_{std::forward<Args>(args)...} {}
    };

    // Deduction guide to automatically deduce N from the number of passed steps
    template<typename... Args>
        requires(sizeof...(Args) > 0)
    CycleSteps(LedState state, Args...) -> CycleSteps<sizeof...(Args)>;

    // The compile-time table of LED cycles, indexed by `LedState`. Cycles have
    // differing step counts, so the element type is a variant of the concrete
    // step-count instantiations.
    using Cycle = std::variant<CycleSteps<1>, CycleSteps<2>>;
    using Cycles = std::array<Cycle, 9>;

    // The table, built entirely at compile time. Returned by value (the
    // constructing function is `constexpr`), which avoids referencing the
    // nested constructors from an in-class static member initializer.
    static constexpr Cycles cycles() noexcept {
        return {
            CycleSteps{LedState::kOff,
                       Step{std::chrono::hours{1}, RedLed::Off, GreenLed::Off}},

            CycleSteps{
                LedState::kBoot,
                Step{std::chrono::milliseconds{20}, RedLed::On, GreenLed::On},
                Step{std::chrono::milliseconds{20}, RedLed::On, GreenLed::Off}},

            CycleSteps{LedState::kConfig,
                       Step{std::chrono::hours{1}, RedLed::On, GreenLed::On}},

            CycleSteps{
                LedState::kConfig_save,
                Step{std::chrono::milliseconds{50}, RedLed::On, GreenLed::Off},
                Step{std::chrono::milliseconds{50}, RedLed::On, GreenLed::On}},

            CycleSteps{
                LedState::kConfig_reset,
                Step{std::chrono::milliseconds{10}, RedLed::On, GreenLed::Off},
                Step{std::chrono::milliseconds{10}, RedLed::On, GreenLed::On}},

            CycleSteps{LedState::kRun,
                       Step{std::chrono::hours{1}, RedLed::Off, GreenLed::On}},
            CycleSteps{
                LedState::kRun_battery_low,
                Step{std::chrono::milliseconds{500}, RedLed::On, GreenLed::Off},
                Step{std::chrono::milliseconds{500}, RedLed::On, GreenLed::On}},

            CycleSteps{LedState::kRun_uploading,
                       Step{std::chrono::milliseconds{100}, RedLed::Off,
                            GreenLed::Off},
                       Step{std::chrono::milliseconds{100}, RedLed::Off,
                            GreenLed::On}},
            CycleSteps{
                LedState::kRun_upload_error,
                Step{std::chrono::milliseconds{100}, RedLed::On, GreenLed::Off},
                Step{std::chrono::milliseconds{100}, RedLed::On, GreenLed::On}},
        };
    }

  public:
    // Whether the compile-time table contains a cycle for `state`. Defined
    // after `cycles()` (which it calls in a constant expression) and exposed so
    // that translation units can `static_assert` full `LedState` coverage ---
    // see the checks at the end of led_task.cpp. A missing entry would
    // otherwise abort at runtime in `lookup_cycle`.
    [[nodiscard]] static constexpr bool has_cycle(LedState state) noexcept {
        constexpr Cycles kTable = cycles();
        for (const Cycle& cycle : kTable) {
            const bool match = std::visit(
                [state](const auto& steps) noexcept {
                    return steps.state_ == state;
                },
                cycle);
            if (match) {
                return true;
            }
        }
        return false;
    }

  private:
    // Change the LEDs to the modes indicated.
    void change_leds(Step const& step) noexcept;

    // Finds the cycle registered for `state` in the table. Aborts if the state
    // has no entry (a programming error: every `LedState` must be covered).
    [[nodiscard]] Cycle const& lookup_cycle(LedState state) const noexcept;

    // Execute until a new message comes in the cycle.
    // cycle: Description on how the LEDs will behave.
    // returns: New state that should be executed.
    template<std::size_t N>
    [[nodiscard]] LedState execute_cycle(const CycleSteps<N>& cycle) noexcept;

    // Applies a state change to the LEDs. Called on the LED task only.
    // Returns the next state when another state change has been
    // sent by set_state.
    [[nodiscard]] LedState on_state_changed(LedState new_state) noexcept;

    // Publishes `msg`, overwriting any message not yet received. Never blocks
    // (the queue has a single slot, so there is always room to overwrite).
    //
    // `Msg` is deduced and constrained to `Derived::Message`. This is a member
    // function template purely to defer naming the (incomplete, at class
    // definition time) `Derived::Message` until first use.
    void send(LedState state) noexcept { queue_.send(state); }

    // Receive, blocking up to `timeout` ticks. Returns the message when one was
    // dequeued, or `std::nullopt` on timeout.
    //
    std::optional<LedState> receive(TickType_t timeout) noexcept {
        return queue_.receive(timeout);
    }

    // Receive, blocking up to `timeout` milliseconds. Convenience overload for
    // callers that prefer a `std::chrono` duration over raw FreeRTOS ticks.
    std::optional<LedState> receive(
        std::chrono::milliseconds timeout) noexcept {
        return queue_.receive(timeout);
    }

    LedState current_state_ = LedState::kOff;

    /** Queue that contains what the new state should be. */
    espidf_cpp::OneMessageQueue<LedState> queue_;
};

class LedTask : protected espidf_cpp::Task<LedRunner> {
  public:
    LedTask();

    LedTask(LedTask const&) = delete;

    LedTask(LedTask&&) = delete;

    ~LedTask() = default;

    LedTask& operator=(LedTask const&) = delete;

    LedTask& operator=(LedTask&&) = delete;

    void set_state(LedState new_state) noexcept {
        runner_.set_state(new_state);
    }
};
}  // namespace thermo
