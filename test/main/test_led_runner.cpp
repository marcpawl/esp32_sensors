// Tests for the templated `LedRunner<Board>`
// (components/logic/include/logic/led_task.hpp, included as
// `logic/led_task.hpp`).
//
// The LED runner is generic over the board (`hal::BoardLike`). Here it is
// instantiated with the pin-free `hal_stub` Board, which proves the template
// compiles and works against a non-hardware board without any ESP32. `run()`
// never returns (it cycles until a new state is published) and blocks waiting
// for state messages on a FreeRTOS queue, so the cycle timing is exercised by
// a whole mocked firmware boot on the stub board; here
// we cover the construction contract and that publishing a state never blocks.
//
// These tests run on the ESP-IDF `linux` target with Unity and link the
// pin-free `hal_stub` component (never `hal_board`).
#include "unity.h"
#include "unity_test_runner.h"

#include <concepts>
#include <memory>
#include <type_traits>

#include "hal/board.hpp"
#include "hal/board_types.hpp"
#include "logic/led_task.hpp"

namespace {

using thermo::LedState;
using thermo::hal::Board;
using thermo::ledtask::LedRunner;

// --- The templated runner satisfies its expected contract -------------------

TEST_CASE("led runner: is generic over a BoardLike board", "[led_runner]") {
    static_assert(thermo::hal::BoardLike<Board>);
    // `create()` must return the same concrete type (part of the concept).
    static_assert(std::same_as<decltype(Board::create({})), Board>);
    // The runner is built from a shared board and is move-only (it owns a
    // queue); there is no default board, so there is no default constructor.
    static_assert(
        std::is_constructible_v<LedRunner<Board>, std::shared_ptr<Board>>);
    static_assert(std::is_move_constructible_v<LedRunner<Board>>);
    static_assert(!std::is_copy_constructible_v<LedRunner<Board>>);
    static_assert(!std::is_default_constructible_v<LedRunner<Board>>);
    TEST_PASS();
}

// --- Construction shares the board -----------------------------------------

TEST_CASE("led runner: shares an initialized stub board", "[led_runner]") {
    auto board = std::make_shared<Board>(Board::create({}));
    TEST_ASSERT_EQUAL(ESP_OK, board->init_all());

    // Construction takes a copy of the shared pointer; it must not drive the
    // LEDs (only the LED task's `run()` loop does that).
    LedRunner<Board> runner(board);

    // The caller keeps sharing the board: one reference here, one in the
    // runner, and the board is still alive after the runner is gone.
    TEST_ASSERT_EQUAL(2, board.use_count());
    {
        LedRunner<Board> second(board);
        TEST_ASSERT_EQUAL(3, board.use_count());
    }
    TEST_ASSERT_EQUAL(2, board.use_count());
}

// --- Publishing a state never blocks ---------------------------------------

TEST_CASE("led runner: set_state overwrites pending states", "[led_runner]") {
    LedRunner<Board> runner(std::make_shared<Board>(Board::create({})));

    // The queue has a single slot: publishing again overwrites the pending
    // state instead of blocking, so this is safe before the task starts.
    runner.set_state(LedState::kBoot);
    runner.set_state(LedState::kConfig);
    runner.set_state(LedState::kRun);
    runner.set_state(LedState::kOff);
}

}  // namespace
