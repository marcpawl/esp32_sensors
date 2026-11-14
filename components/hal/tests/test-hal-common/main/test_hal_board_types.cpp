// Tests for the board HAL vocabulary (`hal/board_types.hpp`).
//
// `BoardPins` is the board's wiring description, defaulting to the §2.4 pin
// map. `BoardLike` is the shared contract both aggregate boards satisfy, and
// the concrete boards are pinned where they live (`test_hal_board.cpp` for the
// stub, the firmware build for `hal_board`). What a bare
// `static_assert(BoardLike<BoardStub>)` cannot prove is that the concept
// *rejects* a board missing a piece: a concept that is too loose compiles
// happily until the first caller relies on the missing member, and then fails
// far from the cause.
//
// So the model below spells out the whole contract, using the real `hal_stub`
// devices so every accessor return type matches the concept exactly. A type
// that is not a class, a class with none of the board interface, and a
// reference are each rejected.
// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#pragma GCC diagnostic pop
// NOLINTEND

#include <chrono>

#include "hal/board_types.hpp"
#include "hal/pin_map.hpp"
#include "hal/sleep_types.hpp"
#include "hal_stub/chip_temperature.hpp"
#include "hal_stub/led.hpp"
#include "hal_stub/onewire.hpp"
#include "hal_stub/push_button.hpp"
#include "hal_stub/wifi.hpp"
#include "unity.h"

namespace {

using thermo::hal::BoardLike;
using thermo::hal::BoardPins;
using thermo::hal::WakeCauses;
using thermo::hal::stub::ChipTemperature;
using thermo::hal::stub::Led;
using thermo::hal::stub::OneWire;
using thermo::hal::stub::PushButton;
using thermo::hal::stub::WifiRadio;

// A class with none of the board interface.
struct NotABoard {};

// A complete board model. The devices are the real stubs, so their types — and
// therefore every accessor's return type — are exactly what the concept asks
// for; only the board itself is a model.
struct CompleteBoard {
    static CompleteBoard create(BoardPins /*pins*/) noexcept { return {}; }

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

}  // namespace

// --- BoardPins defaults -----------------------------------------------------

TEST_CASE("hal board types: BoardPins defaults to the board pin map (§2.4)",
          "[hal_board_types]") {
    // The default wiring comes from `hal/pin_map.hpp`, so a board can be built
    // on the reference hardware without repeating the numbers.
    const BoardPins def{};
    TEST_ASSERT_EQUAL(thermo::pins::kWireBus, def.one_wire);
    TEST_ASSERT_EQUAL(thermo::pins::kGreenLed, def.green_led);
    TEST_ASSERT_EQUAL(thermo::pins::kRedLed, def.red_led);
    TEST_ASSERT_EQUAL(thermo::pins::kConfig, def.config_button);
    TEST_ASSERT_EQUAL(thermo::pins::kRun, def.run_button);
    // The plain button has no dedicated pin on the reference board.
    TEST_ASSERT_EQUAL(0, def.button);
    TEST_ASSERT_EQUAL_STRING("Green", def.green_name);
    TEST_ASSERT_EQUAL_STRING("Red", def.red_name);
}

// --- The concept accepts a complete board ----------------------------------

TEST_CASE("hal board types: a complete board models BoardLike",
          "[hal_board_types]") {
    static_assert(BoardLike<CompleteBoard>);
    TEST_PASS();
}

// --- ...and rejects a type that is not a board -----------------------------

TEST_CASE("hal board types: BoardLike rejects a non-board",
          "[hal_board_types]") {
    static_assert(!BoardLike<NotABoard>);
    // A non-class cannot be a board.
    static_assert(!BoardLike<int>);
    // A reference is not a board: callers own the board.
    static_assert(!BoardLike<CompleteBoard&>);
    TEST_PASS();
}
