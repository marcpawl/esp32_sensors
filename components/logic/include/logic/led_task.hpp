// LED status task: drives the LED indication from a message queue.
//
// Part of the `logic` component: the behaviour here is board-independent. The
// LEDs are driven through a `hal::BoardLike` board, so the same code runs on
// the real GPIO-backed board (`hal_board`, firmware), the pin-free stub
// (`hal_stub`, host tests), or any other conforming board.
#pragma once

// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#pragma GCC diagnostic pop
// NOLINTEND

#include <algorithm>
#include <array>
#include <chrono>
#include <memory>
#include <optional>
#include <utility>
#include <variant>

#include <cstddef>
#include <cstdint>
#include <cstdlib>

#include "espidf_cpp/one_message_queue.hpp"
#include "espidf_cpp/task.hpp"
#include "hal/board.hpp"
#include "hal/board_types.hpp"

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

namespace ledtask {
constexpr const char* kLedTag = "led_task";

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

/** Turns on and off the LEDs.
 *
 * The LEDs are driven through the board, so the runner is independent of how
 * they are implemented: `hal_board` drives real GPIO pins, while `hal_stub`
 * records the state in software (used in host tests).
 *
 * The runner *shares* the board rather than owning it: `LedTask` hands it the
 * same `std::shared_ptr<Board>` the caller keeps, so both can drive/observe the
 * board (e.g. a test keeps its own `shared_ptr` and watches the LEDs the task
 * lights). The board must outlive its last user, which the shared pointer
 * guarantees.
 *
 * @tparam Board A type satisfying `hal::BoardLike` (see `hal/board_types.hpp`).
 */
template<hal::BoardLike Board>
class LedRunner {
  public:
    // Shares ownership of `board`; `board` must not be null.
    explicit LedRunner(std::shared_ptr<Board> board) noexcept
        : board_(std::move(board)) {}

    LedRunner(LedRunner const&) = delete;

    LedRunner(LedRunner&&) = default;

    ~LedRunner() = default;

    LedRunner& operator=(LedRunner const&) = delete;

    LedRunner& operator=(LedRunner&&) = delete;

    // Publishes a new LED state. Safe to call from any task; overwrites any
    // state not yet displayed.
    void set_state(LedState new_state) noexcept { send(new_state); }

    // Task body: waits for state messages and applies them. `[[noreturn]]`,
    // as required by `espidf_cpp::Task<Runner>`.
    [[noreturn]] void run() noexcept;

  private:
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
    // see the coverage checks at the end of this file. A missing entry would
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
    // Change the LEDs to the modes indicated. The board and its LED devices
    // must already be initialized (`Board::init_all()`).
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

    // Publishes `state`, overwriting any message not yet received. Never
    // blocks (the queue has a single slot, so there is always room to
    // overwrite).
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

    /** The board whose LEDs are driven (shared with the caller). */
    std::shared_ptr<Board> board_;

    /** Queue that contains what the new state should be. */
    espidf_cpp::OneMessageQueue<LedState> queue_;
};

template<hal::BoardLike Board>
[[noreturn]] void LedRunner<Board>::run() noexcept {
    LedState state = LedState::kBoot;
    while (true) {
        state = on_state_changed(state);
    }
}

template<hal::BoardLike Board>
Cycle const& LedRunner<Board>::lookup_cycle(LedState state) const noexcept {
    // `cycles()` returns by value, so bind it to a function-local static to
    // give the table (and thus the returned reference) static storage duration.
    static constexpr Cycles kTable = cycles();
    const auto it = std::find_if(kTable.begin(), kTable.end(),
                                 [state](const Cycle& cycle) noexcept {
                                     return std::visit(
                                         [state](const auto& steps) noexcept {
                                             return steps.state_ == state;
                                         },
                                         cycle);
                                 });
    if (it == kTable.end()) {
        ESP_LOGE(kLedTag, "no LED cycle for state %u",
                 static_cast<unsigned>(state));
        std::abort();
    }
    return *it;
}

template<hal::BoardLike Board>
LedState LedRunner<Board>::on_state_changed(LedState new_state) noexcept {
    const Cycle& cycle = lookup_cycle(new_state);
    return std::visit(
        [this](const auto& steps) -> LedState { return execute_cycle(steps); },
        cycle);
}

template<hal::BoardLike Board>
void LedRunner<Board>::change_leds(Step const& step) noexcept {
    // The board is shared, never null (the constructor requires one).
    Board& board = *board_;
    board.red().write(step.red_led_ == RedLed::On ? hal::LedState::kOn
                                                  : hal::LedState::kOff);
    board.green().write(step.green_led_ == GreenLed::On ? hal::LedState::kOn
                                                        : hal::LedState::kOff);
}

template<hal::BoardLike Board>
template<std::size_t N>
[[nodiscard]] LedState LedRunner<Board>::execute_cycle(
    const CycleSteps<N>& cycle) noexcept {
    while (true) {
        for (Step const& step : cycle.steps_) {
            change_leds(step);

            // Wait for a state change for this step's duration; if one
            // arrives, stop the cycle and hand the new state back.
            if (auto next = receive(step.duration_)) {
                if (*next != current_state_) {
                    return *next;
                }
            }
        }
    }
    // Unreachable: the loop above never exits normally.
    [[assume(false)]];
    return LedState::kOff;
}

// Every `LedState` must have a cycle in the table, or `lookup_cycle` would
// abort at runtime. Assert coverage at compile time for each enumerator.
static_assert(LedRunner<hal::Board>::has_cycle(LedState::kOff));
static_assert(LedRunner<hal::Board>::has_cycle(LedState::kBoot));
static_assert(LedRunner<hal::Board>::has_cycle(LedState::kConfig));
static_assert(LedRunner<hal::Board>::has_cycle(LedState::kConfig_save));
static_assert(LedRunner<hal::Board>::has_cycle(LedState::kConfig_reset));
static_assert(LedRunner<hal::Board>::has_cycle(LedState::kRun));
static_assert(LedRunner<hal::Board>::has_cycle(LedState::kRun_battery_low));
static_assert(LedRunner<hal::Board>::has_cycle(LedState::kRun_uploading));
static_assert(LedRunner<hal::Board>::has_cycle(LedState::kRun_upload_error));
}  // namespace ledtask

/** FreeRTOS task that runs a `LedRunner<Board>`.
 *
 * @tparam Board A type satisfying `hal::BoardLike`.
 */
template<hal::BoardLike Board>
class LedTask : protected espidf_cpp::Task<ledtask::LedRunner<Board>> {
  public:
    // Shares `board` with the runner (the caller keeps its own reference) and
    // starts the LED task with it. `board` must not be null.
    explicit LedTask(std::shared_ptr<Board> board);

    LedTask(LedTask const&) = delete;

    LedTask(LedTask&&) = delete;

    ~LedTask() = default;

    LedTask& operator=(LedTask const&) = delete;

    LedTask& operator=(LedTask&&) = delete;

    void set_state(LedState new_state) noexcept {
        // `runner_` lives in the dependent base, so name it through the base
        // type (unqualified lookup would not find it in a template).
        this->runner_.set_state(new_state);
    }
};

template<hal::BoardLike Board>
LedTask<Board>::LedTask(std::shared_ptr<Board> board)
    : espidf_cpp::Task<ledtask::LedRunner<Board>>(
          ledtask::LedRunner<Board>(std::move(board)),
          espidf_cpp::TaskParams()) {}

}  // namespace thermo
