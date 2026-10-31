// Tests for the stub aggregate `BoardStub` (`hal_stub`).
//
// `BoardStub` is a convenience aggregate: it owns one stub device per HAL
// device (chip temperature, 1-Wire bus, green/red LEDs, push buttons, WiFi
// radio) and exposes them. These tests cover the *aggregate* contract —
// construction from `BoardPins`, `init_all()`, that each accessor returns a
// live, usable device, that the underlying stub controls (`set_sensors()`,
// `set()`, `set_celsius()`, `write()`) remain reachable through the board, and
// the board-wide deep sleep (§3.8). The devices' own behaviour is covered by
// their dedicated tests (test_hal_led.cpp, test_hal_push_button.cpp,
// test_hal_onewire.cpp, test_hal_chip_temperature.cpp); here we only verify the
// wiring.
//
// They run on the ESP-IDF `linux` target with Unity and link the pin-free
// `hal_stub` component (never `hal_board`). Every device is deterministic, so
// no hardware is needed.
// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#pragma GCC diagnostic pop
// NOLINTEND

#include <chrono>
#include <concepts>
#include <utility>
#include <vector>

#include <cstdint>

#include "hal/board_stub.hpp"
#include "hal/board_types.hpp"
#include "hal/chip_temperature.hpp"
#include "hal/led.hpp"
#include "hal/onewire.hpp"
#include "hal/pin_map.hpp"
#include "hal/push_button.hpp"
#include "hal/sleep_types.hpp"
#include "hal/wifi.hpp"
#include "unity.h"

namespace {

// Convenience aliases for the shared HAL vocabulary.
using thermo::hal::AccessPointConfig;
using thermo::hal::BoardPins;
using thermo::hal::BoardStub;
using thermo::hal::ButtonState;
using thermo::hal::copy_text;
using thermo::hal::LedState;
using thermo::hal::StationConfig;
using thermo::hal::wake_cause_bit;
using thermo::hal::WakeCause;
using thermo::hal::WakeCauses;
using thermo::hal::woke_from;

// Pin numbers are irrelevant to the stub, but every device takes them for
// interface parity with the GPIO-backed implementation.
constexpr BoardPins kPins{
    .one_wire = 4,
    .green_led = 17,
    .red_led = 16,
    .green_name = "Green",
    .red_name = "Red",
    .button = 0,
    .config_button = 1,
    .run_button = 2,
};

BoardStub make_board() {
    BoardStub b = BoardStub::create(kPins);
    b.init_all();
    return b;
}

}  // namespace

// --- Construction / factory -------------------------------------------------

TEST_CASE("hal board: BoardPins defaults to the board pin map (§2.4)",
          "[hal_board]") {
    // The default wiring comes from `hal/pin_map.hpp`, so a board can be built
    // on the reference hardware without repeating the numbers.
    const BoardPins def{};
    TEST_ASSERT_EQUAL(thermo::pins::kWireBus, def.one_wire);
    TEST_ASSERT_EQUAL(thermo::pins::kGreenLed, def.green_led);
    TEST_ASSERT_EQUAL(thermo::pins::kRedLed, def.red_led);
    TEST_ASSERT_EQUAL(thermo::pins::kConfig, def.config_button);
    TEST_ASSERT_EQUAL(thermo::pins::kRun, def.run_button);

    // A default-constructed board still initializes and works (the stub
    // ignores the pins, but this proves `create({})` is usable).
    BoardStub b = BoardStub::create({});
    TEST_ASSERT_EQUAL(ESP_OK, b.init_all());
}

TEST_CASE("hal board: create() returns a usable, inited board", "[hal_board]") {
    BoardStub b = BoardStub::create(kPins);
    // init_all() always succeeds for the stub (every device init is a no-op).
    TEST_ASSERT_EQUAL(ESP_OK, b.init_all());
    // Each accessor returns a live device.
    TEST_ASSERT_EQUAL(static_cast<int>(ButtonState::kOff),
                      static_cast<int>(b.button().read()));
}

TEST_CASE("hal board: init_all() is idempotent and returns ESP_OK",
          "[hal_board]") {
    BoardStub b = make_board();
    TEST_ASSERT_EQUAL(ESP_OK, b.init_all());
    TEST_ASSERT_EQUAL(ESP_OK, b.init_all());
}

// --- Each accessor wires up an independent, usable device -------------------

TEST_CASE("hal board: chip_temperature() is a live stub device",
          "[hal_board]") {
    BoardStub b = make_board();
    TEST_ASSERT_EQUAL(2500, b.chip_temperature().read());  // default 25.00 °C
    b.chip_temperature().set_celsius(3150);
    TEST_ASSERT_EQUAL(3150, b.chip_temperature().read());
}

TEST_CASE("hal board: one_wire() is a live stub device", "[hal_board]") {
    BoardStub b = make_board();
    // The stub starts with the three example sensors from §5.1.3.
    TEST_ASSERT_EQUAL(3, b.one_wire().read().size());

    const std::vector<thermo::hal::DS18B20Reading> readings = {
        {{0x28, 0xFF, 0x64, 0xA1, 0xB2, 0x16, 0x03, 0xC4}, 2150},
    };
    b.one_wire().set_sensors(readings);
    TEST_ASSERT_EQUAL(1, b.one_wire().discover_sensors().size());
    TEST_ASSERT_EQUAL(1, b.one_wire().read().size());
}

TEST_CASE("hal board: green() and red() are distinct, live stub devices",
          "[hal_board]") {
    BoardStub b = make_board();
    TEST_ASSERT_EQUAL(static_cast<int>(LedState::kOff),
                      static_cast<int>(b.green().state()));
    TEST_ASSERT_EQUAL(static_cast<int>(LedState::kOff),
                      static_cast<int>(b.red().state()));

    b.green().write(LedState::kOn);
    // Writing the green LED must not affect the red one.
    TEST_ASSERT_EQUAL(static_cast<int>(LedState::kOn),
                      static_cast<int>(b.green().state()));
    TEST_ASSERT_EQUAL(static_cast<int>(LedState::kOff),
                      static_cast<int>(b.red().state()));

    b.red().write(LedState::kOn);
    b.green().write(LedState::kOff);
    TEST_ASSERT_EQUAL(static_cast<int>(LedState::kOn),
                      static_cast<int>(b.red().state()));
    TEST_ASSERT_EQUAL(static_cast<int>(LedState::kOff),
                      static_cast<int>(b.green().state()));
}

TEST_CASE("hal board: button() is a live stub device", "[hal_board]") {
    BoardStub b = make_board();
    TEST_ASSERT_EQUAL(static_cast<int>(ButtonState::kOff),
                      static_cast<int>(b.button().read()));
    b.button().set(ButtonState::kOn);
    TEST_ASSERT_EQUAL(static_cast<int>(ButtonState::kOn),
                      static_cast<int>(b.button().read()));
}

TEST_CASE("hal board: config_button() is a live, independent stub device",
          "[hal_board]") {
    BoardStub b = make_board();
    TEST_ASSERT_EQUAL(static_cast<int>(ButtonState::kOff),
                      static_cast<int>(b.config_button().read()));
    b.config_button().set(ButtonState::kOn);
    TEST_ASSERT_EQUAL(static_cast<int>(ButtonState::kOn),
                      static_cast<int>(b.config_button().read()));
    // The mode-sense buttons and the plain button are separate devices.
    TEST_ASSERT_EQUAL(static_cast<int>(ButtonState::kOff),
                      static_cast<int>(b.run_button().read()));
    TEST_ASSERT_EQUAL(static_cast<int>(ButtonState::kOff),
                      static_cast<int>(b.button().read()));
}

TEST_CASE("hal board: run_button() is a live, independent stub device",
          "[hal_board]") {
    BoardStub b = make_board();
    TEST_ASSERT_EQUAL(static_cast<int>(ButtonState::kOff),
                      static_cast<int>(b.run_button().read()));
    b.run_button().set(ButtonState::kOn);
    TEST_ASSERT_EQUAL(static_cast<int>(ButtonState::kOn),
                      static_cast<int>(b.run_button().read()));
    TEST_ASSERT_EQUAL(static_cast<int>(ButtonState::kOff),
                      static_cast<int>(b.config_button().read()));
}

TEST_CASE("hal board: wifi() is a live stub device", "[hal_board]") {
    BoardStub b = make_board();
    // init_all() initializes the radio but starts neither interface.
    TEST_ASSERT_TRUE(b.wifi().initialized());
    TEST_ASSERT_FALSE(b.wifi().ap_running());
    TEST_ASSERT_FALSE(b.wifi().station_running());

    AccessPointConfig ap;
    copy_text(ap.ssid, "thermo-setup");
    copy_text(ap.password, "hunter22");
    TEST_ASSERT_EQUAL(ESP_OK, b.wifi().start_ap(ap));
    TEST_ASSERT_TRUE(b.wifi().ap_running());
    TEST_ASSERT_EQUAL_STRING("thermo-setup", b.wifi().ap_config()->ssid.data());

    StationConfig sta;
    copy_text(sta.ssid, "home");
    TEST_ASSERT_EQUAL(ESP_OK, b.wifi().start_station(sta));
    TEST_ASSERT_TRUE(b.wifi().station_running());
    // The two interfaces are independent: both may run at once (§3.2).
    TEST_ASSERT_TRUE(b.wifi().ap_running());

    TEST_ASSERT_EQUAL(ESP_OK, b.wifi().stop_ap());
    TEST_ASSERT_FALSE(b.wifi().ap_running());
    TEST_ASSERT_TRUE(b.wifi().station_running());
}

// --- Deep sleep (§3.8) ------------------------------------------------------

TEST_CASE("hal board: a fresh stub reports a cold boot, not a wake",
          "[hal_board]") {
    const BoardStub b = make_board();
    // ESP-IDF reports BIT(ESP_SLEEP_WAKEUP_UNDEFINED) while running normally,
    // so the stub models a cold boot the same way.
    TEST_ASSERT_EQUAL_UINT32(wake_cause_bit(WakeCause::kUndefined),
                             b.sleep_get_wakeup_causes());
    TEST_ASSERT_TRUE(
        woke_from(b.sleep_get_wakeup_causes(), WakeCause::kUndefined));
    TEST_ASSERT_FALSE(
        woke_from(b.sleep_get_wakeup_causes(), WakeCause::kTimer));
    // Nothing has been asked for yet.
    TEST_ASSERT_FALSE(b.last_deep_sleep().has_value());
}

TEST_CASE("hal board: deep_sleep_start() records the duration and returns",
          "[hal_board]") {
    BoardStub b = make_board();

    // The stub returns (the hardware board does not), which is what makes the
    // sleep/backoff decision host-testable.
    b.deep_sleep_start(std::chrono::seconds{60});
    TEST_ASSERT_TRUE(b.last_deep_sleep().has_value());
    TEST_ASSERT_EQUAL(60, b.last_deep_sleep()->count());

    // A later request overwrites the earlier one.
    b.deep_sleep_start(std::chrono::seconds{3600});
    TEST_ASSERT_EQUAL(3600, b.last_deep_sleep()->count());

    // Sleeping arms the wake the real board would see: it enables the RTC
    // timer (§3.8), so the next boot reports a timer wake.
    TEST_ASSERT_EQUAL_UINT32(wake_cause_bit(WakeCause::kTimer),
                             b.sleep_get_wakeup_causes());
    TEST_ASSERT_TRUE(woke_from(b.sleep_get_wakeup_causes(), WakeCause::kTimer));
    TEST_ASSERT_FALSE(
        woke_from(b.sleep_get_wakeup_causes(), WakeCause::kUndefined));
}

TEST_CASE("hal board: set_wakeup_causes() overrides the modelled wake",
          "[hal_board]") {
    BoardStub b = make_board();

    // Before any sleep the board is running normally, i.e. a cold boot.
    b.set_wakeup_causes(wake_cause_bit(WakeCause::kUndefined));
    TEST_ASSERT_EQUAL_UINT32(wake_cause_bit(WakeCause::kUndefined),
                             b.sleep_get_wakeup_causes());
    TEST_ASSERT_FALSE(
        woke_from(b.sleep_get_wakeup_causes(), WakeCause::kTimer));

    // A test can also force a wake without sleeping first.
    b.set_wakeup_causes(wake_cause_bit(WakeCause::kTimer));
    TEST_ASSERT_TRUE(woke_from(b.sleep_get_wakeup_causes(), WakeCause::kTimer));

    // Wake causes are a bitmap: several sources can fire at once.
    b.set_wakeup_causes(wake_cause_bit(WakeCause::kTimer) |
                        wake_cause_bit(WakeCause::kUndefined));
    TEST_ASSERT_TRUE(woke_from(b.sleep_get_wakeup_causes(), WakeCause::kTimer));
    TEST_ASSERT_TRUE(
        woke_from(b.sleep_get_wakeup_causes(), WakeCause::kUndefined));

    // ...but sleeping again re-arms the timer wake the board would produce.
    b.deep_sleep_start(std::chrono::seconds{5});
    TEST_ASSERT_EQUAL_UINT32(wake_cause_bit(WakeCause::kTimer),
                             b.sleep_get_wakeup_causes());

    b.set_wakeup_causes(0);
    TEST_ASSERT_EQUAL_UINT32(0, b.sleep_get_wakeup_causes());
}

TEST_CASE("hal board: the wake-cause vocabulary is a bit per wake source",
          "[hal_board]") {
    // One bit per source, in the position named by the enumerator, so the
    // hardware board can pass ESP-IDF's `BIT(ESP_SLEEP_WAKEUP_*)` bitmap
    // through unchanged.
    TEST_ASSERT_EQUAL_UINT32(1u << 0, wake_cause_bit(WakeCause::kUndefined));
    TEST_ASSERT_EQUAL_UINT32(1u << 4, wake_cause_bit(WakeCause::kTimer));
    // `kTimer` is ESP_SLEEP_WAKEUP_TIMER (4) in ESP-IDF, and `kUndefined` is
    // 0; `board.cpp` asserts the same thing on the hardware side.
    static_assert(static_cast<std::uint32_t>(WakeCause::kUndefined) == 0);
    static_assert(static_cast<std::uint32_t>(WakeCause::kTimer) == 4);
}

// --- The board exposes the const accessors too ------------------------------

TEST_CASE("hal board: const accessors yield const device references",
          "[hal_board]") {
    const BoardStub b = make_board();
    // Reading through a const board is well-formed (proves the const overloads
    // exist and return the const-qualified device types).
    TEST_ASSERT_EQUAL(2500, b.chip_temperature().read());
    TEST_ASSERT_EQUAL(static_cast<int>(ButtonState::kOff),
                      static_cast<int>(b.button().read()));
    TEST_ASSERT_EQUAL(static_cast<int>(ButtonState::kOff),
                      static_cast<int>(b.config_button().read()));
    TEST_ASSERT_EQUAL(static_cast<int>(ButtonState::kOff),
                      static_cast<int>(b.run_button().read()));
    // The 1-Wire stub defaults to the three example sensors from §5.1.3.
    TEST_ASSERT_EQUAL(3, b.one_wire().read().size());
    TEST_ASSERT_EQUAL(static_cast<int>(LedState::kOff),
                      static_cast<int>(b.green().state()));
    TEST_ASSERT_EQUAL(static_cast<int>(LedState::kOff),
                      static_cast<int>(b.red().state()));
    // The radio was inited by init_all(); neither interface is running, so the
    // const observation accessors are meaningful.
    TEST_ASSERT_TRUE(b.wifi().initialized());
    TEST_ASSERT_FALSE(b.wifi().ap_running());
    TEST_ASSERT_FALSE(b.wifi().station_running());
}

// --- The stub satisfies the shared concept ---------------------------------

TEST_CASE("hal board: stub BoardStub models BoardLike", "[hal_board]") {
    static_assert(thermo::hal::BoardLike<BoardStub>);
    // create() must return the same concrete type (part of the concept).
    static_assert(std::same_as<decltype(BoardStub::create(kPins)), BoardStub>);
    // Deep sleep is part of the contract, so the concept covers it. The stub
    // returns from it while the hardware board does not, which is why the
    // concept only requires that the call is well-formed.
    static_assert(
        std::same_as<decltype(std::declval<BoardStub&>().deep_sleep_start(
                         std::chrono::seconds{1})),
                     void>);
    static_assert(std::same_as<decltype(std::declval<BoardStub const&>()
                                            .sleep_get_wakeup_causes()),
                               WakeCauses>);
    TEST_ASSERT_EQUAL(ESP_OK, make_board().init_all());
}
