// Tests for the templated `LedRunner<Board>`
// (mcgowan/src/include/led_task.hpp).
//
// The LED runner is generic over the board (`hal::BoardLike`). Here it is
// instantiated with the pin-free `hal_stub` Board, which proves the template
// compiles and works against a non-hardware board without any ESP32. `run()`
// never returns (it cycles until a new state is published) and blocks waiting
// for state messages on a FreeRTOS queue, so the cycle timing is exercised by
// the QEMU smoke test (which runs the whole firmware on the stub board); here
// we cover the construction contract and that publishing a state never blocks.
//
// These tests run on the ESP-IDF `linux` target with Unity and link the
// pin-free `hal_stub` component (never `hal_board`).
#include "unity.h"
#include "unity_test_runner.h"

#include <type_traits>
#include <utility>

#include "hal/board.hpp"
#include "hal/board_types.hpp"
#include "led_task.hpp"

namespace {

using thermo::LedRunner;
using thermo::LedState;
using thermo::hal::Board;

// --- The templated runner satisfies its expected contract -------------------

TEST_CASE("led runner: is generic over a BoardLike board", "[led_runner]") {
    static_assert(thermo::hal::BoardLike<Board>);
    // `create()` must return the same concrete type (part of the concept).
    static_assert(std::same_as<decltype(Board::create({})), Board>);
    // The runner is built from a board and is move-only (it owns a queue);
    // there is no default board, so there is no default constructor.
    static_assert(std::is_constructible_v<LedRunner<Board>, Board>);
    static_assert(std::is_move_constructible_v<LedRunner<Board>>);
    static_assert(!std::is_copy_constructible_v<LedRunner<Board>>);
    static_assert(!std::is_default_constructible_v<LedRunner<Board>>);
    TEST_PASS();
}

// --- Construction takes ownership of the board ------------------------------

TEST_CASE("led runner: accepts an initialized stub board", "[led_runner]") {
    Board board = Board::create({});
    TEST_ASSERT_EQUAL(ESP_OK, board.init_all());

    // Construction moves the board in; it must not drive the LEDs (only the
    // LED task's `run()` loop does that).
    LedRunner<Board> runner(std::move(board));
    (void)runner;
}

// --- Publishing a state never blocks ---------------------------------------

TEST_CASE("led runner: set_state overwrites pending states", "[led_runner]") {
    LedRunner<Board> runner(Board::create({}));

    // The queue has a single slot: publishing again overwrites the pending
    // state instead of blocking, so this is safe before the task starts.
    runner.set_state(LedState::kBoot);
    runner.set_state(LedState::kConfig);
    runner.set_state(LedState::kRun);
    runner.set_state(LedState::kOff);
}

}  // namespace
