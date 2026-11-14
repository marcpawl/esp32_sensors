// Tests for the stub aggregate `BoardStub` (`hal_stub`).
//
// `BoardStub` is a convenience aggregate: it owns one stub device per HAL
// device (chip temperature, 1-Wire bus, green/red LEDs, push buttons, WiFi
// radio, one-shot timer) and exposes them. These tests cover the *aggregate*
// contract — construction from `BoardPins`, `init_all()`, that each accessor
// returns a live, usable device, that the underlying stub controls
// (`set_sensors()`, `set()`, `set_celsius()`, `write()`) remain reachable
// through the board, and the board-wide deep sleep (§3.8). The devices' own
// behaviour is covered by their dedicated tests (test_hal_led.cpp,
// test_hal_push_button.cpp, test_hal_onewire.cpp,
// test_hal_chip_temperature.cpp, test_hal_timer_stub.cpp); here we only verify
// the wiring.
//
// The one-shot timer is the exception to "the wiring only": its stub callback
// really runs, in the FreeRTOS timer service task, so `register_timer()`
// through the board is exercised end-to-end here (with bounded polls, exactly
// as `test_hal_timer_stub.cpp` does).
//
// They run on the ESP-IDF `linux` target with Unity and link the pin-free
// `hal_stub` component (never `hal_board`). Every device is deterministic, so
// no hardware is needed.
// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#pragma GCC diagnostic pop
// NOLINTEND

#include <atomic>
#include <chrono>
#include <concepts>
#include <functional>
#include <utility>
#include <vector>

#include <cstdint>

#include "hal/board_types.hpp"
#include "hal/sleep_types.hpp"
#include "hal/timer_types.hpp"
#include "hal_stub/board_stub.hpp"
#include "hal_stub/chip_temperature.hpp"
#include "hal_stub/led.hpp"
#include "hal_stub/onewire.hpp"
#include "hal_stub/push_button.hpp"
#include "hal_stub/timer_stub.hpp"
#include "hal_stub/wifi.hpp"
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
using thermo::hal::TimerLike;
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

// --- Bounded polling for the one-shot timer ---------------------------------
//
// The stub timer runs on FreeRTOS software timers, whose commands are
// asynchronous and whose callback arrives from the timer service task, so every
// assertion about it has to wait rather than read once. The waits are bounded
// so a timer that never fires fails the test instead of hanging it.
//
// `TickType_t` is spelled explicitly because on this port `pdMS_TO_TICKS()` is
// not an assignable lvalue, and `configTICK_RATE_HZ` is 100 (a tick is 10 ms).
constexpr std::chrono::milliseconds kTimerDelay{200};
constexpr TickType_t kPollTicks = 20 * configTICK_RATE_HZ / 1000;
constexpr TickType_t kLimitTicks = 4000 * configTICK_RATE_HZ / 1000;
constexpr TickType_t kQuietTicks =
    (kTimerDelay * 10).count() * configTICK_RATE_HZ / 1000;

// Waits (bounded) for `calls` to reach `expected` and returns what was seen.
int wait_for_calls(std::atomic<int>& calls, int expected) {
    for (TickType_t waited = 0; calls.load() < expected && waited < kLimitTicks;
         waited += kPollTicks) {
        vTaskDelay(kPollTicks);
    }
    return calls.load();
}

// Gives the timer many periods to misbehave, then returns the count.
int calls_after_quiet_period(std::atomic<int>& calls) {
    vTaskDelay(kQuietTicks);
    return calls.load();
}

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

// --- One-shot timer ---------------------------------------------------------

TEST_CASE("hal board: the board names a cancelable TimerHandle type",
          "[hal_board]") {
    // The `BoardLike` contract requires a public member typedef, and the
    // handle it names must be cancelable — that is all a holder needs.
    static_assert(std::same_as<BoardStub::TimerHandle, thermo::hal::TimerStub>);
    static_assert(TimerLike<BoardStub::TimerHandle>);
    TEST_PASS();
}

TEST_CASE("hal board: register_timer() returns the board's TimerHandle",
          "[hal_board]") {
    static_assert(
        std::same_as<decltype(std::declval<BoardStub&>().register_timer(
                         kTimerDelay, std::function<void()>{})),
                     BoardStub::TimerHandle>);
    TEST_PASS();
}

TEST_CASE("hal board: a timer armed through the board fires exactly once",
          "[hal_board]") {
    // A function-local static: the callback may outlive this test (cancellation
    // is racy by contract), so it must not write to a stack local — and naming
    // it directly keeps the lambda capture-less, which is what clang-tidy's
    // unused-lambda-capture check requires.
    static std::atomic<int> calls;
    calls.store(0);

    BoardStub b = make_board();
    BoardStub::TimerHandle handle =
        b.register_timer(kTimerDelay, [] { calls.fetch_add(1); });
    TEST_ASSERT_TRUE(handle.valid());

    // The callback really runs, in the timer service task...
    TEST_ASSERT_EQUAL_INT(1, wait_for_calls(calls, 1));
    // ...and only once, however many further periods elapse.
    TEST_ASSERT_EQUAL_INT(1, calls_after_quiet_period(calls));
}

TEST_CASE("hal board: a timer armed through the board can be cancelled",
          "[hal_board]") {
    static std::atomic<int> calls;
    calls.store(0);

    BoardStub b = make_board();
    BoardStub::TimerHandle handle =
        b.register_timer(kTimerDelay, [] { calls.fetch_add(1); });
    // Cancelled while still armed, so the callback never runs.
    handle.cancel();
    TEST_ASSERT_FALSE(handle.valid());
    TEST_ASSERT_EQUAL_INT(0, calls_after_quiet_period(calls));
}

TEST_CASE("hal board: the board arms independent timers", "[hal_board]") {
    // Two registrations through one board must produce two calls, so the board
    // really does hand each one its own timer.
    static std::atomic<int> calls;
    calls.store(0);

    BoardStub b = make_board();
    BoardStub::TimerHandle a =
        b.register_timer(kTimerDelay, [] { calls.fetch_add(1); });
    BoardStub::TimerHandle other =
        b.register_timer(kTimerDelay, [] { calls.fetch_add(1); });
    TEST_ASSERT_TRUE(a.valid());
    TEST_ASSERT_TRUE(other.valid());

    TEST_ASSERT_EQUAL_INT(2, wait_for_calls(calls, 2));
    TEST_ASSERT_EQUAL_INT(2, calls_after_quiet_period(calls));
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
    // The one-shot timer is part of the same contract: the board names the
    // handle type and arms timers through it.
    static_assert(TimerLike<BoardStub::TimerHandle>);
    static_assert(
        std::same_as<decltype(std::declval<BoardStub&>().register_timer(
                         kTimerDelay, std::function<void()>{})),
                     BoardStub::TimerHandle>);
    TEST_ASSERT_EQUAL(ESP_OK, make_board().init_all());
}
