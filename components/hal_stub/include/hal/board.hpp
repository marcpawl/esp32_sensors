// Hardware abstraction: the aggregate Board, deterministic, pin-free stub for
// QEMU and host tests.
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
// test or QEMU scenario can drive and inspect the whole board deterministically
// without hardware.
#pragma once

#include "esp_err.h"
#include "hal/board_types.hpp"
#include "hal/chip_temperature.hpp"
#include "hal/led.hpp"
#include "hal/onewire.hpp"
#include "hal/push_button.hpp"
#include "hal/switch.hpp"

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

    // The 3-position mode switch (stub; pins via set_pins()).
    [[nodiscard]] Switch& mode_switch() noexcept { return mode_switch_; }

    [[nodiscard]] Switch const& mode_switch() const noexcept {
        return mode_switch_;
    }

    // The WiFi radio (stub; state via ap_running() / station_running(),
    // configuration via ap_config() / station_config()).
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
