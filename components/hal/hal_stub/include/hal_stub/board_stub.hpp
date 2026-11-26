// Hardware abstraction: the aggregate `BoardStub`, deterministic, pin-free stub
// for the host tests.
//
// The shared board vocabulary (`BoardPins`, `BoardLike`) lives in the
// header-only `hal_common` component (`hal/board_types.hpp`); this header adds
// the concrete `BoardStub` class whose devices are the `hal_stub`
// implementations. `hal_board` provides its own `Board` with the same shape
// (inline `create()` factory, `init_all()`, and accessors) whose devices are
// the real, GPIO-backed ones.
//
// The stub's class is deliberately named `BoardStub` rather than `Board`: the
// two HAL implementations are mutually exclusive, so giving the stub its own
// name lets a project name the implementation it actually linked instead of
// relying on which aggregate-board header happened to be on the include path.
// Code
// that must work with either keeps taking the `hal::BoardLike` template
// parameter (`logic/led_task.hpp`).
//
// Because every device is a `hal_stub` device, their `set_*()` controls and
// observation accessors are reachable through the board's accessors (e.g.
// `board().one_wire().set_sensors(...)`, `board().wifi().ap_running()`), so a
// test can drive and inspect the whole board deterministically
// without hardware. Deep sleep (§3.8) is stubbed the same way: instead of
// powering the board down, `deep_sleep_start()` records the requested duration,
// arms the wake cause the real board would wake from (`WakeCause::kTimer`), and
// returns, so a test can follow one sleep cycle into the next boot. A test
// wanting a different wake overrides it with `set_wakeup_causes()`.
#pragma once

// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#include "esp_err.h"
#pragma GCC diagnostic pop
// NOLINTEND

#include <chrono>
#include <optional>

#include <cstdint>

#include "hal/board_types.hpp"
#include "hal/sleep_types.hpp"
#include "hal_stub/chip_temperature.hpp"
#include "hal_stub/led.hpp"
#include "hal_stub/onewire.hpp"
#include "hal_stub/wifi.hpp"

namespace thermo::hal::stub {

// Owns every stub HAL device on the board.
class BoardStub {
  public:
    // The concrete device types a template derives from the board
    // (`BoardLike` requires both).
    using LedType = Led;

    enum class Power : std::uint8_t { Off = 0, On };

    // `pins` describes the wiring; see `BoardPins`. The stub ignores the pin
    // numbers but keeps the LED labels for logging. Constructs the devices but
    // does not touch any hardware — call init_all() for that.
    explicit BoardStub(BoardPins pins) noexcept;

    BoardStub(BoardStub const&) = delete;
    // Movable so `create()` (and containers/factories) can hand a BoardStub
    // back by value without needing guaranteed copy elision.
    BoardStub(BoardStub&&) noexcept = default;
    ~BoardStub() noexcept = default;
    BoardStub& operator=(BoardStub const&) = delete;
    BoardStub& operator=(BoardStub&&) = delete;

    // Builds a board for the given wiring. Same signature for the hardware and
    // stub implementations; call init_all() before using the devices.
    [[nodiscard]] static BoardStub create(BoardPins pins) noexcept {
        // GCC's analyzer cannot model libstdc++'s std::string/std::vector
        // internals and reports an uninitialised read while returning the
        // board by value. The code is correct; keep the diagnostic fatal
        // everywhere else.
        // NOLINTBEGIN
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wanalyzer-use-of-uninitialized-value"
        return BoardStub(pins);
#pragma GCC diagnostic pop
        // NOLINTEND
    }

    // Initializes every device (all no-ops for the stub), returning the first
    // error encountered. Always succeeds for the stub.
    esp_err_t init_all() noexcept;

    // The on-die chip temperature sensor (stub; reading via set_celsius()).
    [[nodiscard]] ChipTemperature& chip_temperature() noexcept {
        return chip_temperature_;
    }

    [[nodiscard]] ChipTemperature const& chip_temperature() const noexcept {
        return chip_temperature_;
    }

    // The DS18B20 1-Wire bus (stub; sensors via set_sensors()).
    [[nodiscard]] OneWire& one_wire() noexcept { return one_wire_; }

    [[nodiscard]] OneWire const& one_wire() const noexcept { return one_wire_; }

    // The green status LED (stub; state via write()).
    [[nodiscard]] Led& green() noexcept { return green_; }

    [[nodiscard]] Led const& green() const noexcept { return green_; }

    // The red status LED (stub; state via write()).
    [[nodiscard]] Led& red() noexcept { return red_; }

    [[nodiscard]] Led const& red() const noexcept { return red_; }

    // The WiFi radio (stub; state via ap_running() / station_running(),
    // configuration via ap_config() / station_config()).
    [[nodiscard]] WifiRadio& wifi() noexcept { return wifi_; }

    [[nodiscard]] WifiRadio const& wifi() const noexcept { return wifi_; }

    [[nodiscard]] Power power() const noexcept { return power_; }

    void set_power(Power power) noexcept { power_ = power; }

    // --- Deep sleep (§3.8) -------------------------------------------------

    // Stubbed deep sleep: records `duration`, arms the wake cause the real
    // board would wake from, and returns, so a test can carry on and assert
    // what the firmware asked for. Nothing is powered down and no state is
    // reset — it is the caller's job to act on the wake cause.
    //
    // Because `hal_board` enables the RTC timer as the wake source
    // (`esp_sleep_enable_timer_wakeup()`), the stub models the wake that
    // follows as a timer wake: after this call,
    // `sleep_get_wakeup_causes()` reports `WakeCause::kTimer`. A test that
    // needs a different wake (e.g. a cold boot, or several sources at once)
    // overrides it with `set_wakeup_causes()`.
    void deep_sleep_start(std::chrono::seconds duration) noexcept;

    // The wake causes the current boot reports. Starts as a cold boot
    // (`WakeCause::kUndefined`, which is what ESP-IDF reports while running
    // normally) and becomes `WakeCause::kTimer` once `deep_sleep_start()` has
    // been called; `set_wakeup_causes()` overrides either.
    [[nodiscard]] WakeCauses sleep_get_wakeup_causes() const noexcept;

    // Sets the wake causes reported by `sleep_get_wakeup_causes()` (stub only;
    // not part of the concept). Pass `0` for "no cause known" and
    // `wake_cause_bit(WakeCause::kUndefined)` for a cold boot. Call it after
    // `deep_sleep_start()` to simulate a wake other than the timer.
    void set_wakeup_causes(WakeCauses causes) noexcept;

    // The duration most recently passed to `deep_sleep_start()`, or
    // `std::nullopt` if it has not been called (stub only; not part of the
    // concept).
    [[nodiscard]] std::optional<std::chrono::seconds> last_deep_sleep()
        const noexcept;

  private:
    ChipTemperature chip_temperature_;
    OneWire one_wire_;
    Led green_;
    Led red_;
    WifiRadio wifi_;
    // Wake causes reported by `sleep_get_wakeup_causes()`. A fresh stub
    // models a cold boot, which is what the real board reports while running
    // normally.
    WakeCauses wakeup_causes_ = wake_cause_bit(WakeCause::kUndefined);
    // Duration last passed to `deep_sleep_start()`, if any.
    std::optional<std::chrono::seconds> last_deep_sleep_;
    Power power_ = Power::Off;
};

static_assert(BoardLike<BoardStub>,
              "BoardStub must satisfy the BoardLike concept");

}  // namespace thermo::hal::stub
