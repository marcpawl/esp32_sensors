// Native link check for the aggregate Board HAL (`hal_stub`).
//
// Proves the stub Board links and works under real FreeRTOS on the linux
// target: `create()` with the default pin map, `init_all()`, and each accessor
// returning a live device. The host suite in `test/` covers the aggregate
// wiring in depth.
#include "unity.h"

#include <vector>

#include "hal/board.hpp"
#include "hal/board_types.hpp"
#include "hal/led_types.hpp"
#include "hal/push_button_types.hpp"
#include "hal/switch_types.hpp"

namespace {
using thermo::hal::Board;
using thermo::hal::ButtonState;
using thermo::hal::DS18B20Reading;
using thermo::hal::LedState;
using thermo::hal::PinState;
using thermo::hal::RomId;

const RomId kSensor{0x28, 0xFF, 0x3B, 0x2C, 0x77, 0x16, 0x04, 0x2F};
}  // namespace

TEST_CASE("espidf_cpp_test board: create/init_all and drives every accessor",
          "[espidf_cpp_test][board]") {
    static_assert(thermo::hal::BoardLike<Board>);

    // `BoardPins{}` uses the reference-board pin map (hal/pin_map.hpp); the
    // stub ignores the pins anyway.
    Board board = Board::create({});
    TEST_ASSERT_EQUAL(ESP_OK, board.init_all());

    board.chip_temperature().set_celsius(3150);
    TEST_ASSERT_EQUAL_INT(3150, board.chip_temperature().read());

    board.one_wire().set_sensors(std::vector<DS18B20Reading>{{kSensor, 2150}});
    TEST_ASSERT_EQUAL_UINT(
        1, static_cast<unsigned>(board.one_wire().read().size()));

    board.green().write(LedState::kOn);
    TEST_ASSERT_EQUAL(static_cast<int>(LedState::kOn),
                      static_cast<int>(board.green().state()));

    board.button().set(ButtonState::kOn);
    TEST_ASSERT_EQUAL(static_cast<int>(ButtonState::kOn),
                      static_cast<int>(board.button().read()));

    const thermo::hal::Pins run{PinState::kOff, PinState::kOn};
    board.mode_switch().set_pins(run);
    TEST_ASSERT_TRUE(board.mode_switch().read() == run);

    board.network_time_protocol().set_utc_epoch(1700000000u);
    const auto epoch =
        board.network_time_protocol().get_utc_epoch("time.nrc.ca");
    TEST_ASSERT_TRUE(epoch.has_value());
    TEST_ASSERT_EQUAL_UINT32(1700000000u, *epoch);
}
