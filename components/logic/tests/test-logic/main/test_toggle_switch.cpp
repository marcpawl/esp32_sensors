// Tests for `ToggleSwitch<Board>` (`logic/toggle_switch.hpp`, issue #51).
//
// The switch HAL is semantics-free: the stub's `set()` simulates an edge
// synchronously, and a real board delivers one from the GPIO ISR. The toggle
// wrapper supplies the debounce and stability semantics on top.
//
// These tests use the stub board and deliberately short windows so they stay
// deterministic and fast: 20 ms debounce, 200 ms stability. The esp_timer host
// implementation runs its callbacks in the FreeRTOS timer task, so a test
// yields with `vTaskDelay` and then inspects the recorder.
// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#pragma GCC diagnostic pop
// NOLINTEND

#include <atomic>
#include <chrono>
#include <concepts>
#include <optional>

#include "hal/board_types.hpp"
#include "hal/switch_types.hpp"
#include "hal_stub/board_stub.hpp"
#include "hal_stub/switch.hpp"
#include "logic/toggle_switch.hpp"
#include "unity.h"

namespace {

using thermo::ToggleSwitch;
using thermo::hal::BoardPins;
using thermo::hal::SwitchState;
using thermo::hal::stub::BoardStub;
using thermo::hal::stub::Switch;

constexpr std::chrono::milliseconds kDebounce{20};
constexpr std::chrono::milliseconds kStability{200};

// The host esp_timer implementation rounds to FreeRTOS ticks (100 Hz in this
// project), so leave several ticks for the debounce plus the timer task.
constexpr TickType_t kSettleWait = pdMS_TO_TICKS(80);

// GCC's analyzer cannot model libstdc++'s std::string internals and reports
// the board returned by create() as uninitialised. The code is correct; keep
// the diagnostic fatal everywhere else.
// NOLINTBEGIN
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wanalyzer-use-of-uninitialized-value"

BoardStub make_board() {
    BoardStub board = BoardStub::create(BoardPins{});
    board.init_all();
    return board;
}

#pragma GCC diagnostic pop

// NOLINTEND

// Records callbacks from the timer task. Atomics keep the test task's reads
// well-defined.
struct Recorder {
    std::atomic<int> calls{0};
    std::atomic<int> last{static_cast<int>(SwitchState::kOff)};

    void record(SwitchState state) noexcept {
        calls.fetch_add(1, std::memory_order_relaxed);
        last.store(static_cast<int>(state), std::memory_order_relaxed);
    }
};

}  // namespace

// --- Contract ---------------------------------------------------------------

TEST_CASE("toggle switch: BoardStub supplies the switch type",
          "[toggle_switch]") {
    static_assert(
        std::same_as<typename ToggleSwitch<BoardStub>::SwitchType, Switch>);
    static_assert(std::same_as<typename ToggleSwitch<BoardStub>::CallbackFn,
                               std::function<void(SwitchState)>>);
}

TEST_CASE("toggle switch: defaults are the issue's windows",
          "[toggle_switch]") {
    static_assert(thermo::toggle::kDebounce == std::chrono::milliseconds{50});
    static_assert(thermo::toggle::kStability == std::chrono::seconds{10});
}

TEST_CASE("toggle switch: the stability boundary is exact", "[toggle_switch]") {
    using thermo::toggle::burst_exceeded;
    constexpr std::chrono::milliseconds kWindow{100};

    static_assert(!burst_exceeded(99'999, 0, kWindow));
    static_assert(burst_exceeded(100'000, 0, kWindow));
    static_assert(burst_exceeded(150'000, 0, kWindow));
}

// --- Initial settle ---------------------------------------------------------

TEST_CASE("toggle switch: reports the initial state after the debounce window",
          "[toggle_switch]") {
    BoardStub board = make_board();
    Recorder rec;
    ToggleSwitch<BoardStub> toggle(
        board.user_switch(), [&rec](SwitchState s) { rec.record(s); },
        kDebounce, kStability);

    // The first callback waits for the debounce window, so nothing is settled
    // yet.
    TEST_ASSERT_FALSE(toggle.get_state().has_value());
    TEST_ASSERT_EQUAL(0, rec.calls.load());

    vTaskDelay(kSettleWait);

    TEST_ASSERT_EQUAL(1, rec.calls.load());
    TEST_ASSERT_EQUAL(static_cast<int>(SwitchState::kOff), rec.last.load());
    TEST_ASSERT_TRUE(toggle.get_state().has_value());
    TEST_ASSERT_EQUAL(static_cast<int>(SwitchState::kOff),
                      static_cast<int>(*toggle.get_state()));
}

// --- A single edge ----------------------------------------------------------

TEST_CASE("toggle switch: a close reports once with the new state",
          "[toggle_switch]") {
    BoardStub board = make_board();
    Recorder rec;
    ToggleSwitch<BoardStub> toggle(
        board.user_switch(), [&rec](SwitchState s) { rec.record(s); },
        kDebounce, kStability);
    vTaskDelay(kSettleWait);
    rec.calls = 0;

    board.user_switch().set(SwitchState::kOn);
    vTaskDelay(kSettleWait);

    TEST_ASSERT_EQUAL(1, rec.calls.load());
    TEST_ASSERT_EQUAL(static_cast<int>(SwitchState::kOn), rec.last.load());
    TEST_ASSERT_EQUAL(static_cast<int>(SwitchState::kOn),
                      static_cast<int>(*toggle.get_state()));
}

// --- Bounce suppression -----------------------------------------------------

TEST_CASE("toggle switch: edges inside the window report only the final state",
          "[toggle_switch]") {
    BoardStub board = make_board();
    Recorder rec;
    ToggleSwitch<BoardStub> toggle(
        board.user_switch(), [&rec](SwitchState s) { rec.record(s); },
        kDebounce, kStability);
    vTaskDelay(kSettleWait);
    rec.calls = 0;

    // Contact bounce: the timer restarts on every edge, so only the last
    // state settles.
    board.user_switch().set(SwitchState::kOn);
    board.user_switch().set(SwitchState::kOff);
    board.user_switch().set(SwitchState::kOn);
    vTaskDelay(kSettleWait);

    TEST_ASSERT_EQUAL(1, rec.calls.load());
    TEST_ASSERT_EQUAL(static_cast<int>(SwitchState::kOn), rec.last.load());
}

TEST_CASE("toggle switch: a later edge reports again", "[toggle_switch]") {
    BoardStub board = make_board();
    Recorder rec;
    ToggleSwitch<BoardStub> toggle(
        board.user_switch(), [&rec](SwitchState s) { rec.record(s); },
        kDebounce, kStability);
    vTaskDelay(kSettleWait);

    board.user_switch().set(SwitchState::kOn);
    vTaskDelay(kSettleWait);
    TEST_ASSERT_EQUAL(2, rec.calls.load());
    TEST_ASSERT_EQUAL(static_cast<int>(SwitchState::kOn), rec.last.load());

    board.user_switch().set(SwitchState::kOff);
    vTaskDelay(kSettleWait);
    TEST_ASSERT_EQUAL(3, rec.calls.load());
    TEST_ASSERT_EQUAL(static_cast<int>(SwitchState::kOff), rec.last.load());
}

// --- Lifetime ---------------------------------------------------------------

TEST_CASE("toggle switch: destruction unregisters the switch interrupt",
          "[toggle_switch]") {
    BoardStub board = make_board();
    Recorder rec;
    {
        ToggleSwitch<BoardStub> toggle(
            board.user_switch(), [&rec](SwitchState s) { rec.record(s); },
            kDebounce, kStability);
        vTaskDelay(kSettleWait);
        rec.calls = 0;
    }

    // The interrupt is unregistered, so an edge after destruction reaches no
    // handler and the timer is gone.
    board.user_switch().set(SwitchState::kOn);
    vTaskDelay(kSettleWait);
    TEST_ASSERT_EQUAL(0, rec.calls.load());
}

TEST_CASE("toggle switch: destruction waits for an in-flight debounce",
          "[toggle_switch]") {
    BoardStub board = make_board();
    Recorder rec;
    {
        // Destroy while the initial debounce is still armed; the destructor
        // must stop the timer without the callback touching freed state.
        ToggleSwitch<BoardStub> toggle(
            board.user_switch(), [&rec](SwitchState s) { rec.record(s); },
            kDebounce, kStability);
    }
    vTaskDelay(kSettleWait);
    TEST_ASSERT_EQUAL(0, rec.calls.load());
}
