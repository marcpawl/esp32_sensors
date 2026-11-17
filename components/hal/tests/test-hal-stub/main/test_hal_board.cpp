// Tests for the stub aggregate `BoardStub` (`hal_stub`).
//
// `BoardStub` is a convenience aggregate: it owns one stub device per HAL
// device (chip temperature, 1-Wire bus, green/red LEDs, switches, WiFi
// radio) and exposes them. These tests cover the *aggregate* contract —
// construction from `BoardPins`, `init_all()`, that each accessor returns a
// live, usable device, that the underlying stub controls (`set_sensors()`,
// `set()`, `set_celsius()`, `write()`) remain reachable through the board, and
// the board-wide deep sleep (§3.8). The devices' own behaviour is covered by
// their dedicated tests (test_hal_led.cpp, test_hal_switch.cpp,
// test_hal_onewire.cpp, test_hal_chip_temperature.cpp); here we only verify
// the wiring.
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

#include <cstdint>

#include "hal/board_types.hpp"
#include "hal/sleep_types.hpp"
#include "hal_stub/board_stub.hpp"
#include "hal_stub/chip_temperature.hpp"
#include "hal_stub/led.hpp"
#include "hal_stub/onewire.hpp"
#include "hal_stub/switch.hpp"
#include "hal_stub/wifi.hpp"
#include "unity.h"

namespace {

// Convenience aliases for the shared HAL vocabulary.
using thermo::hal::AccessPointConfig;
using thermo::hal::BoardPins;
using thermo::hal::copy_text;
using thermo::hal::LedState;
using thermo::hal::StationConfig;
using thermo::hal::SwitchState;
using thermo::hal::wake_cause_bit;
using thermo::hal::WakeCause;
using thermo::hal::WakeCauses;
using thermo::hal::woke_from;
using thermo::hal::stub::BoardStub;

// Pin numbers are irrelevant to the stub, but every device takes them for
// interface parity with the GPIO-backed implementation.
constexpr BoardPins kPins{
    .one_wire = 4,
    .green_led = 17,
    .red_led = 16,
    .green_name = "Green",
    .red_name = "Red",
    .user_switch = 0,
    .config_switch = 1,
    .run_switch = 2,
};

// GCC's analyzer cannot model libstdc++'s std::string internals and
// reports the board returned by create() as uninitialised. The code is
// correct; keep the diagnostic fatal everywhere else.
// NOLINTBEGIN
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wanalyzer-use-of-uninitialized-value"

BoardStub make_board() {
    BoardStub b = BoardStub::create(kPins);
    b.init_all();
    return b;
}

#pragma GCC diagnostic pop
// NOLINTEND

}  // namespace

// --- Construction / factory -------------------------------------------------

TEST_CASE("hal board: create() accepts the default BoardPins", "[hal_board]") {
    // The defaults come from `hal/pin_map.hpp`; the stub ignores the pins, but
    // this proves `BoardStub::create({})` is usable on the reference wiring.
    // The pin values themselves are tested with the vocabulary that defines
    // them (`test-hal-common/main/test_hal_board_types.cpp`).
    BoardStub b = BoardStub::create({});
    TEST_ASSERT_EQUAL(ESP_OK, b.init_all());
}

TEST_CASE("hal board: create() returns a usable, inited board", "[hal_board]") {
    BoardStub b = BoardStub::create(kPins);
    // init_all() always succeeds for the stub (every device init is a no-op).
    TEST_ASSERT_EQUAL(ESP_OK, b.init_all());
    // Each accessor returns a live device.
    TEST_ASSERT_EQUAL(static_cast<int>(SwitchState::kOff),
                      static_cast<int>(b.user_switch().read()));
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

    thermo::hal::ReadingList readings;
    readings.push_back(
        {{0x28, 0xFF, 0x64, 0xA1, 0xB2, 0x16, 0x03, 0xC4}, 2150});
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

TEST_CASE("hal board: user_switch() is a live stub device", "[hal_board]") {
    BoardStub b = make_board();
    TEST_ASSERT_EQUAL(static_cast<int>(SwitchState::kOff),
                      static_cast<int>(b.user_switch().read()));
    b.user_switch().set(SwitchState::kOn);
    TEST_ASSERT_EQUAL(static_cast<int>(SwitchState::kOn),
                      static_cast<int>(b.user_switch().read()));
}

TEST_CASE("hal board: config_switch() is a live, independent stub device",
          "[hal_board]") {
    BoardStub b = make_board();
    TEST_ASSERT_EQUAL(static_cast<int>(SwitchState::kOff),
                      static_cast<int>(b.config_switch().read()));
    b.config_switch().set(SwitchState::kOn);
    TEST_ASSERT_EQUAL(static_cast<int>(SwitchState::kOn),
                      static_cast<int>(b.config_switch().read()));
    // The mode-sense poles and the plain switch are separate devices.
    TEST_ASSERT_EQUAL(static_cast<int>(SwitchState::kOff),
                      static_cast<int>(b.run_switch().read()));
    TEST_ASSERT_EQUAL(static_cast<int>(SwitchState::kOff),
                      static_cast<int>(b.user_switch().read()));
}

TEST_CASE("hal board: run_switch() is a live, independent stub device",
          "[hal_board]") {
    BoardStub b = make_board();
    TEST_ASSERT_EQUAL(static_cast<int>(SwitchState::kOff),
                      static_cast<int>(b.run_switch().read()));
    b.run_switch().set(SwitchState::kOn);
    TEST_ASSERT_EQUAL(static_cast<int>(SwitchState::kOn),
                      static_cast<int>(b.run_switch().read()));
    TEST_ASSERT_EQUAL(static_cast<int>(SwitchState::kOff),
                      static_cast<int>(b.config_switch().read()));
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
    const std::optional<AccessPointConfig>& running_ap = b.wifi().ap_config();
    TEST_ASSERT_TRUE(running_ap.has_value());
    TEST_ASSERT_EQUAL_STRING("thermo-setup", running_ap->ssid.data());

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
    TEST_ASSERT_TRUE(b.last_deep_sleep().has_value());
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

// --- The board exposes the const accessors too ------------------------------

TEST_CASE("hal board: const accessors yield const device references",
          "[hal_board]") {
    const BoardStub b = make_board();
    // Reading through a const board is well-formed (proves the const overloads
    // exist and return the const-qualified device types).
    TEST_ASSERT_EQUAL(2500, b.chip_temperature().read());
    TEST_ASSERT_EQUAL(static_cast<int>(SwitchState::kOff),
                      static_cast<int>(b.user_switch().read()));
    TEST_ASSERT_EQUAL(static_cast<int>(SwitchState::kOff),
                      static_cast<int>(b.config_switch().read()));
    TEST_ASSERT_EQUAL(static_cast<int>(SwitchState::kOff),
                      static_cast<int>(b.run_switch().read()));
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
