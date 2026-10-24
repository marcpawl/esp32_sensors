// Hardware abstraction: the aggregate Board, real, GPIO-backed implementation
// for the development board.
//
// The shared board vocabulary (`BoardPins`, `BoardLike`) lives in the
// header-only `hal_common` component (`hal/board_types.hpp`); this header adds
// the concrete `Board` class. `hal_stub` provides an identical `Board` (same
// inline `create()` factory, `init_all()`, and accessors) whose devices are the
// pin-free stubs, so a project can link either one and include
// `<hal/board.hpp>` unchanged.
//
// The board owns one instance of each HAL device (chip temperature, 1-Wire bus,
// green/red LEDs, push button, mode switch) and exposes them. It carries no
// behaviour of its own beyond delegating `init_all()` to the devices.
#pragma once

// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#include "esp_err.h"
#pragma GCC diagnostic pop
// NOLINTEND

#include "hal/board_types.hpp"
#include "hal/chip_temperature.hpp"
#include "hal/led.hpp"
#include "hal/onewire.hpp"
#include "hal/push_button.hpp"
#include "hal/switch.hpp"
#include "hal/wifi.hpp"

namespace thermo::hal {

// Owns every HAL device on the board.
class Board {
  public:
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

    // The push button.
    [[nodiscard]] PushButton& button() noexcept { return button_; }

    [[nodiscard]] PushButton const& button() const noexcept { return button_; }

    // The 3-position mode switch.
    [[nodiscard]] Switch& mode_switch() noexcept { return mode_switch_; }

    [[nodiscard]] Switch const& mode_switch() const noexcept {
        return mode_switch_;
    }

    // The WiFi radio (§3.2, §3.9). Initialized by init_all(), but neither
    // interface is started: the caller decides when to bring the AP (§3.2) or
    // the station (§3.9) up.
    [[nodiscard]] WifiRadio& wifi() noexcept { return wifi_; }

    [[nodiscard]] WifiRadio const& wifi() const noexcept { return wifi_; }

  private:
    ChipTemperature chip_temperature_;
    OneWire one_wire_;
    Led green_;
    Led red_;
    PushButton button_;
    Switch mode_switch_;
    WifiRadio wifi_;
};

static_assert(BoardLike<Board>, "Board must satisfy the BoardLike concept");

}  // namespace thermo::hal
