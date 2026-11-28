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
#include "hal_stub/wifi.hpp"
#include "unity.h"

namespace {

using thermo::hal::BoardLike;
using thermo::hal::BoardPins;
using thermo::hal::Gpio;
using thermo::hal::WakeCauses;
using thermo::hal::stub::ChipTemperature;
using thermo::hal::stub::Led;
using thermo::hal::stub::OneWire;
using thermo::hal::stub::WifiRadio;

// A class with none of the board interface.
struct NotABoard {};

// The board interface, without the nested device-type aliases. Both models
// below add `create()`, so the aliases are the only difference between the
// accepted board and the rejected one.
struct BoardDeviceSet {
    esp_err_t init_all() noexcept { return ESP_OK; }

    void deep_sleep_start(std::chrono::seconds /*duration*/) noexcept {}

    [[nodiscard]] WakeCauses sleep_get_wakeup_causes() const noexcept
    {
        return 0;
    }

    [[nodiscard]] ChipTemperature& chip_temperature() noexcept
    {
        return chip_temperature_;
    }

    [[nodiscard]] ChipTemperature const& chip_temperature() const noexcept
    {
        return chip_temperature_;
    }

    [[nodiscard]] OneWire& one_wire() noexcept { return one_wire_; }

    [[nodiscard]] OneWire const& one_wire() const noexcept { return one_wire_; }

    [[nodiscard]] Led& green() noexcept { return green_; }

    [[nodiscard]] Led const& green() const noexcept { return green_; }

    [[nodiscard]] Led& red() noexcept { return red_; }

    [[nodiscard]] Led const& red() const noexcept { return red_; }

    [[nodiscard]] WifiRadio& wifi() noexcept { return wifi_; }

    [[nodiscard]] WifiRadio const& wifi() const noexcept { return wifi_; }

  protected:
    ChipTemperature chip_temperature_;
    OneWire one_wire_ = OneWire::create(Gpio{4});
    Led green_ = Led::create(Gpio{17}, "Green");
    Led red_ = Led::create(Gpio{16}, "Red");
    WifiRadio wifi_;
};

// A complete board model: the full device set plus the nested aliases the
// concept requires.
struct CompleteBoard : BoardDeviceSet {
    using LedType = Led;

    static CompleteBoard create(BoardPins /*pins*/) noexcept { return {}; }
};

// The same interface, but without `LedType` / `SwitchType`. A board that omits
// them cannot serve a board-independent template, so `BoardLike` must reject
// it.
struct BoardWithoutDeviceTypes : BoardDeviceSet {
    static BoardWithoutDeviceTypes create(BoardPins /*pins*/) noexcept
    {
        return {};
    }
};

}  // namespace

// --- BoardPins defaults -----------------------------------------------------

TEST_CASE("hal board types: BoardPins defaults to the board pin map (§2.4)",
          "[hal_board_types]")
{
    // The default wiring comes from `hal/pin_map.hpp`, so a board can be built
    // on the reference hardware without repeating the numbers.
    const BoardPins def{};
    TEST_ASSERT_EQUAL(thermo::pins::kWireBus.as_int(), def.one_wire.as_int());
    TEST_ASSERT_EQUAL(thermo::pins::kGreenLed.as_int(), def.green_led.as_int());
    TEST_ASSERT_EQUAL(thermo::pins::kRedLed.as_int(), def.red_led.as_int());
    // The plain (user) switch has no dedicated pin on the reference board.
    TEST_ASSERT_EQUAL(0, def.user_switch.as_int());
    TEST_ASSERT_EQUAL_STRING("Green", def.green_name);
    TEST_ASSERT_EQUAL_STRING("Red", def.red_name);
}

// --- The concept accepts a complete board ----------------------------------

TEST_CASE("hal board types: a complete board models BoardLike",
          "[hal_board_types]")
{
    static_assert(BoardLike<CompleteBoard>);
    TEST_PASS();
}

TEST_CASE("hal board types: BoardLike rejects a board without device types",
          "[hal_board_types]")
{
    // The device interface is complete, but `LedType` / `SwitchType` are
    // missing, so a template could not derive them from the board.
    static_assert(!BoardLike<BoardWithoutDeviceTypes>);
    TEST_PASS();
}

// --- ...and rejects a type that is not a board -----------------------------

TEST_CASE("hal board types: BoardLike rejects a non-board", "[hal_board_types]")
{
    static_assert(!BoardLike<NotABoard>);
    // A non-class cannot be a board.
    static_assert(!BoardLike<int>);
    // A reference is not a board: callers own the board.
    static_assert(!BoardLike<CompleteBoard&>);
    TEST_PASS();
}
