// Hardware abstraction: the aggregate Board, real, GPIO-backed implementation
// for the development board.
//
// The shared board vocabulary (`BoardPins`, `BoardLike`) lives in the
// header-only `hal_common` component (`hal/board_types.hpp`); this header adds
// the concrete `Board` class. `hal_stub` provides an identical `Board` (same
// inline `create()` factory, `init_all()`, and accessors) whose devices are the
// pin-free stubs, so a project can link either one and include its own
// aggregate-board header (`hal_board/board.hpp` or `hal_stub/board_stub.hpp`).
//
// The board owns one instance of each HAL device (chip temperature, 1-Wire bus,
// green/red LEDs, user switch, mode-sense switch poles) and exposes them. It
// carries no behaviour of its own beyond delegating `init_all()` to the
// devices, plus deep sleep (§3.8), which belongs to the board rather than to a
// peripheral because it powers the whole board down.
#pragma once

// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#include "esp_err.h"
#pragma GCC diagnostic pop
// NOLINTEND

#include <chrono>

#include "hal/board_types.hpp"
#include "hal/sleep_types.hpp"
#include "hal_board/chip_temperature.hpp"
#include "hal_board/led.hpp"
#include "hal_board/onewire.hpp"
#include "hal_board/switch.hpp"
#include "hal_board/wifi.hpp"

namespace thermo::hal::board {

// Owns every HAL device on the board.
class Board {
  public:
    // The concrete device types a template derives from the board
    // (`BoardLike` requires both).
    using LedType = Led;
    using SwitchType = Switch;

    // `pins` describes the wiring; see `BoardPins`. Constructs the devices but
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

    // Initializes every device, returning the first error encountered (and
    // stopping there). Call once before using the board.
    esp_err_t init_all() noexcept;

    // The on-die chip temperature sensor.
    [[nodiscard]] ChipTemperature& chip_temperature() noexcept {
        return chip_temperature_;
    }

    [[nodiscard]] ChipTemperature const& chip_temperature() const noexcept {
        return chip_temperature_;
    }

    // The DS18B20 1-Wire bus.
    [[nodiscard]] OneWire& one_wire() noexcept { return one_wire_; }

    [[nodiscard]] OneWire const& one_wire() const noexcept { return one_wire_; }

    // The green status LED.
    [[nodiscard]] Led& green() noexcept { return green_; }

    [[nodiscard]] Led const& green() const noexcept { return green_; }

    // The red status LED.
    [[nodiscard]] Led& red() noexcept { return red_; }

    [[nodiscard]] Led const& red() const noexcept { return red_; }

    // The plain (user) switch.
    [[nodiscard]] Switch& user_switch() noexcept { return user_switch_; }

    [[nodiscard]] Switch const& user_switch() const noexcept {
        return user_switch_;
    }

    // The Configure-mode sense switch (one pole of the 3-position switch).
    [[nodiscard]] Switch& config_switch() noexcept { return config_switch_; }

    [[nodiscard]] Switch const& config_switch() const noexcept {
        return config_switch_;
    }

    // The Run-mode sense switch (one pole of the 3-position switch).
    [[nodiscard]] Switch& run_switch() noexcept { return run_switch_; }

    [[nodiscard]] Switch const& run_switch() const noexcept {
        return run_switch_;
    }

    // The WiFi radio (§3.2, §3.9). Initialized by init_all(), but neither
    // interface is started: the caller decides when to bring the AP (§3.2) or
    // the station (§3.9) up.
    [[nodiscard]] WifiRadio& wifi() noexcept { return wifi_; }

    [[nodiscard]] WifiRadio const& wifi() const noexcept { return wifi_; }

    // Enters deep sleep for `duration` (§3.8): enables the RTC timer as the
    // wake source and then sleeps, so the next boot happens `duration` later.
    //
    // Does not return: `esp_deep_sleep_start()` restarts the MCU, and this
    // function is therefore `[[noreturn]]` here even though `BoardLike` can
    // only require that it is callable (the stub must return). Callers may not
    // rely on control coming back.
    [[noreturn]] void deep_sleep_start(std::chrono::seconds duration) noexcept;

    // The wake sources that caused the last wake from sleep (§3.8), as the
    // ESP-IDF bitmap. Consulted on boot; a cold boot reports only
    // `WakeCause::kUndefined`.
    [[nodiscard]] WakeCauses sleep_get_wakeup_causes() const noexcept;

  private:
    ChipTemperature chip_temperature_;
    OneWire one_wire_;
    Led green_;
    Led red_;
    Switch user_switch_;
    Switch config_switch_;
    Switch run_switch_;
    WifiRadio wifi_;
};

static_assert(BoardLike<Board>, "Board must satisfy the BoardLike concept");

}  // namespace thermo::hal::board
