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
//
// The one-shot timer is owned by the board as well: `register_timer()` arms an
// `OneShotTimerFactoryStub` and returns the board's `TimerHandle` (here
// `TimerStub`). A test therefore arms a timer through the board and can
// observe it firing, exactly as the firmware does — and because the stub is
// built on FreeRTOS software timers, that callback really does run.
#pragma once

// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#include "esp_err.h"
#pragma GCC diagnostic pop
// NOLINTEND

#include <chrono>
#include <functional>
#include <optional>
#include <utility>

#include <cstdint>

#include "hal/board_types.hpp"
#include "hal/sleep_types.hpp"
#include "hal_stub/chip_temperature.hpp"
#include "hal_stub/led.hpp"
#include "hal_stub/onewire.hpp"
#include "hal_stub/push_button.hpp"
#include "hal_stub/timer_stub.hpp"
#include "hal_stub/wifi.hpp"

namespace thermo::hal::stub {

// Owns every stub HAL device on the board.
class BoardStub {
  public:
    // The type `register_timer()` returns: the FreeRTOS-backed handle. A caller
    // only needs `cancel()`, so board-independent code holds it as
    // `BoardStub::TimerHandle` rather than naming `TimerStub`.
    using TimerHandle = TimerStub;

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
        return BoardStub(pins);
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

    // The push button (stub; state via set()).
    [[nodiscard]] PushButton& button() noexcept { return button_; }

    [[nodiscard]] PushButton const& button() const noexcept { return button_; }

    // The Configure-mode sense button (stub; state via set()).
    [[nodiscard]] PushButton& config_button() noexcept {
        return config_button_;
    }

    [[nodiscard]] PushButton const& config_button() const noexcept {
        return config_button_;
    }

    // The Run-mode sense button (stub; state via set()).
    [[nodiscard]] PushButton& run_button() noexcept { return run_button_; }

    [[nodiscard]] PushButton const& run_button() const noexcept {
        return run_button_;
    }

    // The WiFi radio (stub; state via ap_running() / station_running(),
    // configuration via ap_config() / station_config()).
    [[nodiscard]] WifiRadio& wifi() noexcept { return wifi_; }

    [[nodiscard]] WifiRadio const& wifi() const noexcept { return wifi_; }

    // --- One-shot timer ----------------------------------------------------

    // Schedules `callback` to run once after `delay` and returns the handle
    // that owns the timer. The callback runs in the FreeRTOS timer service
    // task; capture whatever state it needs in the callable.
    //
    // Part of the `BoardLike` contract, so callers arm timers through the board
    // and never name `OneShotTimerFactoryStub` / `TimerStub`. Fails
    // loudly (an assert) if the delay rounds to fewer than one tick, the
    // callback is null, or the timer cannot be created or started.
    //
    // The callback really runs (this is a live timer, not a recording), so a
    // test observes it the way `test_hal_timer_stub.cpp` does: poll with
    // `vTaskDelay()` rather than reading a flag immediately.
    [[nodiscard]] TimerHandle register_timer(std::chrono::milliseconds delay,
                                             std::function<void()> callback) {
        return timer_.register_timer(delay, std::move(callback));
    }

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
    PushButton button_;
    PushButton config_button_;
    PushButton run_button_;
    WifiRadio wifi_;
    // Arms the board's one-shot timers. Not initialized by `init_all()`: the
    // FreeRTOS timer service needs no per-board setup, and an armed timer is
    // independent of every other device.
    OneShotTimerFactoryStub timer_;
    // Wake causes reported by `sleep_get_wakeup_causes()`. A fresh stub
    // models a cold boot, which is what the real board reports while running
    // normally.
    WakeCauses wakeup_causes_ = wake_cause_bit(WakeCause::kUndefined);
    // Duration last passed to `deep_sleep_start()`, if any.
    std::optional<std::chrono::seconds> last_deep_sleep_;
};

static_assert(BoardLike<BoardStub>,
              "BoardStub must satisfy the BoardLike concept");

}  // namespace thermo::hal::stub
