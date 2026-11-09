// Tests for the board HAL vocabulary (`hal/board_types.hpp`).
//
// `BoardLike` is the shared contract both aggregate boards satisfy, and the
// concrete boards are pinned where they live (`test_hal_board.cpp` for the
// stub, the firmware build for `hal_board`). What a bare
// `static_assert(BoardLike<BoardStub>)` cannot prove is that the concept
// *rejects* a board missing a piece: a concept that is too loose compiles
// happily until the first caller relies on the missing member, and then fails
// far from the cause.
//
// So the models below exist to be rejected one requirement at a time.
// `BoardWithoutTimer` spells out everything `BoardLike` requires *except* the
// timer, using the real `hal_stub` devices so every accessor return type
// matches the concept exactly. `ModelBoard<Handle, Returned>` derives from it
// and adds the timer contract alone — the public `TimerHandle` typedef and a
// `register_timer()` returning `Returned` (default `Handle`, so only a broken
// case names it). Each rejection therefore isolates a single requirement.
//
// The timer is the requirement Step 4 added, which is why it gets the closest
// attention here; the other clauses are pinned by the two real boards'
// `static_assert`s.
// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#pragma GCC diagnostic pop
// NOLINTEND

#include <chrono>
#include <concepts>
#include <functional>

#include "hal/board_types.hpp"
#include "hal/chip_temperature.hpp"
#include "hal/led.hpp"
#include "hal/onewire.hpp"
#include "hal/push_button.hpp"
#include "hal/sleep_types.hpp"
#include "hal/timer_stub.hpp"
#include "hal/timer_types.hpp"
#include "hal/wifi.hpp"
#include "unity.h"

namespace {

using thermo::hal::BoardLike;
using thermo::hal::BoardPins;
using thermo::hal::ChipTemperature;
using thermo::hal::Led;
using thermo::hal::OneWire;
using thermo::hal::PushButton;
using thermo::hal::TimerHandleLike;
using thermo::hal::TimerHandleStub;
using thermo::hal::WakeCauses;
using thermo::hal::WifiRadio;

// A handle that is a class but has no `cancel()`: exactly the mistake a board
// could make, and which `TimerHandleLike` must reject.
struct NotCancelable {
    void stop() {}
};

// A second, *cancelable* handle. Lets "the return type must match the board's
// own typedef" be tested without also tripping `TimerHandleLike`.
struct OtherHandle {
    void cancel() {}
};

// A class with none of the board interface.
struct NotABoard {};

// Every requirement of `BoardLike` except the timer. The devices are the real
// stubs, so their types — and therefore every accessor's return type — are
// exactly what the concept asks for; only the board itself is a model.
struct BoardWithoutTimer {
    static BoardWithoutTimer create(BoardPins /*pins*/) noexcept { return {}; }

    esp_err_t init_all() noexcept { return ESP_OK; }

    void deep_sleep_start(std::chrono::seconds /*duration*/) noexcept {}

    [[nodiscard]] WakeCauses sleep_get_wakeup_causes() const noexcept {
        return 0;
    }

    [[nodiscard]] ChipTemperature& chip_temperature() noexcept {
        return chip_temperature_;
    }

    [[nodiscard]] ChipTemperature const& chip_temperature() const noexcept {
        return chip_temperature_;
    }

    [[nodiscard]] OneWire& one_wire() noexcept { return one_wire_; }

    [[nodiscard]] OneWire const& one_wire() const noexcept { return one_wire_; }

    [[nodiscard]] Led& green() noexcept { return green_; }

    [[nodiscard]] Led const& green() const noexcept { return green_; }

    [[nodiscard]] Led& red() noexcept { return red_; }

    [[nodiscard]] Led const& red() const noexcept { return red_; }

    [[nodiscard]] PushButton& button() noexcept { return button_; }

    [[nodiscard]] PushButton const& button() const noexcept { return button_; }

    [[nodiscard]] PushButton& config_button() noexcept {
        return config_button_;
    }

    [[nodiscard]] PushButton const& config_button() const noexcept {
        return config_button_;
    }

    [[nodiscard]] PushButton& run_button() noexcept { return run_button_; }

    [[nodiscard]] PushButton const& run_button() const noexcept {
        return run_button_;
    }

    [[nodiscard]] WifiRadio& wifi() noexcept { return wifi_; }

    [[nodiscard]] WifiRadio const& wifi() const noexcept { return wifi_; }

  private:
    ChipTemperature chip_temperature_;
    OneWire one_wire_ = OneWire::create(4);
    Led green_ = Led::create(17, "Green");
    Led red_ = Led::create(16, "Red");
    PushButton button_ = PushButton::create(0);
    PushButton config_button_ = PushButton::create(1);
    PushButton run_button_ = PushButton::create(2);
    WifiRadio wifi_;
};

// A complete board model: `BoardWithoutTimer` plus the timer contract.
//
// `Handle` is the public `TimerHandle` typedef; `Returned` is what
// `register_timer()` actually returns. Defaulting `Returned` to `Handle` makes
// the well-formed case the default, so a test only names the one thing it
// breaks.
template<typename Handle, typename Returned = Handle>
struct ModelBoard : BoardWithoutTimer {
    using TimerHandle = Handle;

    // The base's `create()` returns `BoardWithoutTimer`, which would fail the
    // concept's `create(...) -> same_as<T>` clause for every model. Each model
    // states its own, so a rejection always points at the timer alone.
    static ModelBoard create(BoardPins /*pins*/) noexcept { return {}; }

    [[nodiscard]] Returned register_timer(
        std::chrono::milliseconds /*delay*/,
        std::function<void()> /*callback*/) const noexcept {
        return {};
    }
};

// Everything right except that there is no `TimerHandle` typedef at all: a
// caller would have nothing to name the handle by. `register_timer()` is
// spelled with its concrete return type, so the absence is genuine rather than
// an inaccessible or hidden name.
struct NoTimerHandleTypedef : BoardWithoutTimer {
    // See `ModelBoard`: the return type has to name this model, not the base.
    static NoTimerHandleTypedef create(BoardPins /*pins*/) noexcept {
        return {};
    }

    [[nodiscard]] TimerHandleStub register_timer(
        std::chrono::milliseconds /*delay*/,
        std::function<void()> /*callback*/) const noexcept {
        return {};
    }
};

// Everything right except that there is no `register_timer()`.
struct NoRegisterTimer : BoardWithoutTimer {
    // See `ModelBoard`: the return type has to name this model, not the base.
    static NoRegisterTimer create(BoardPins /*pins*/) noexcept { return {}; }

    using TimerHandle = TimerHandleStub;
};

}  // namespace

// --- The concept accepts a complete board ----------------------------------

TEST_CASE("hal board types: a complete board models BoardLike",
          "[hal_board_types]") {
    static_assert(BoardLike<ModelBoard<TimerHandleStub>>);
    // The handle the model names is cancelable, which is the whole of what
    // `BoardLike` asks of `TimerHandle`.
    static_assert(TimerHandleLike<ModelBoard<TimerHandleStub>::TimerHandle>);
    TEST_PASS();
}

// --- ...and rejects every board that is missing the timer -------------------

TEST_CASE("hal board types: BoardLike requires a public TimerHandle type",
          "[hal_board_types]") {
    // The requirement Step 4 added: without the typedef, `Board::TimerHandle`
    // does not exist and a caller cannot declare the handle it is given.
    static_assert(!BoardLike<NoTimerHandleTypedef>);
    TEST_PASS();
}

TEST_CASE("hal board types: BoardLike requires a TimerHandle that is a class",
          "[hal_board_types]") {
    // `void` is not a class, so `TimerHandleLike` rejects it: a board cannot
    // satisfy the concept by naming a "handle" nobody could hold.
    static_assert(!TimerHandleLike<void>);
    static_assert(!BoardLike<ModelBoard<void>>);
    TEST_PASS();
}

TEST_CASE("hal board types: BoardLike requires a cancelable TimerHandle",
          "[hal_board_types]") {
    // The typedef is a class, but the handle has no `cancel()`, so a caller
    // could never disarm a timer.
    static_assert(!TimerHandleLike<NotCancelable>);
    static_assert(!BoardLike<ModelBoard<NotCancelable>>);
    TEST_PASS();
}

TEST_CASE("hal board types: BoardLike pins the register_timer return type",
          "[hal_board_types]") {
    // Both handles are cancelable, so the only thing wrong is that
    // `register_timer()` returns one type while the board names another: a
    // caller storing the result as `TimerHandle` would not compile.
    static_assert(TimerHandleLike<OtherHandle>);
    static_assert(!BoardLike<ModelBoard<TimerHandleStub, OtherHandle>>);
    TEST_PASS();
}

TEST_CASE("hal board types: BoardLike requires register_timer() to exist",
          "[hal_board_types]") {
    // The typedef is there and the board is complete in every other respect, so
    // the rejection is the absent method and nothing else. (`BoardWithoutTimer`
    // alone would fail on its inherited `create()` return type instead, so the
    // model has to restate that one.)
    static_assert(!BoardLike<NoRegisterTimer>);
    TEST_PASS();
}

TEST_CASE("hal board types: BoardLike rejects a non-board",
          "[hal_board_types]") {
    static_assert(!BoardLike<NotABoard>);
    // A non-class cannot be a board.
    static_assert(!BoardLike<int>);
    // A reference is not a board: callers own the board.
    static_assert(!BoardLike<ModelBoard<TimerHandleStub>&>);
    TEST_PASS();
}
