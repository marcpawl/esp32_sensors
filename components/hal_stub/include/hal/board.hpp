// Hardware abstraction: the aggregate Board, deterministic, pin-free stub for
// the host tests.
//
// The shared board vocabulary (`BoardPins`, `BoardLike`) lives in the
// header-only `hal_common` component (`hal/board_types.hpp`); this header adds
// the concrete `Board` class whose devices are the `hal_stub` implementations.
// `hal_board` provides an identical `Board` (same inline `create()` factory,
// `init_all()`, and accessors) whose devices are the real, GPIO-backed ones, so
// a project can link either one and include `<hal/board.hpp>` unchanged.
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
#include "hal/chip_temperature.hpp"
#include "hal/led.hpp"
#include "hal/onewire.hpp"
#include "hal/push_button.hpp"
#include "hal/sleep_types.hpp"
#include "hal/wifi.hpp"

namespace thermo::hal {

// Owns every stub HAL device on the board.
class Board {
  public:
    // `pins` describes the wiring; see `BoardPins`. The stub ignores the pin
    // numbers but keeps the LED labels for logging. Constructs the devices but
    // does not touch any hardware — call init_all() for that.
    explicit Board(BoardPins pins) noexcept;

    Board(Board const&) = delete;
    // Movable so `create()` (and containers/factories) can hand a Board back by
    // value without needing guaranteed copy elision.
    Board(Board&&) noexcept = default;
    ~Board() noexcept = default;
    Board& operator=(Board const&) = delete;
    Board& operator=(Board&&) = delete;

    // Builds a board for the given wiring. Same signature for the hardware and
    // stub implementations; call init_all() before using the devices.
    [[nodiscard]] static Board create(BoardPins pins) noexcept {
        return Board(pins);
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
    // Wake causes reported by `sleep_get_wakeup_causes()`. A fresh stub
    // models a cold boot, which is what the real board reports while running
    // normally.
    WakeCauses wakeup_causes_ = wake_cause_bit(WakeCause::kUndefined);
    // Duration last passed to `deep_sleep_start()`, if any.
    std::optional<std::chrono::seconds> last_deep_sleep_;
};

static_assert(BoardLike<Board>, "Board must satisfy the BoardLike concept");

}  // namespace thermo::hal
