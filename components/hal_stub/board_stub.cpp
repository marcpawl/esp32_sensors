// Deterministic, pin-free aggregate Board stub for the host tests.
//
// Constructs one `hal_stub` device per board device and initializes them in a
// fixed order. Every stub `init()` is a no-op that succeeds, so `init_all()`
// always returns ESP_OK without touching hardware.
// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#pragma GCC diagnostic pop
// NOLINTEND

#include "hal/board.hpp"

namespace thermo::hal {

Board::Board(BoardPins pins) noexcept
    : chip_temperature_(),
      one_wire_(OneWire::create(pins.one_wire)),
      green_(Led::create(pins.green_led, pins.green_name)),
      red_(Led::create(pins.red_led, pins.red_name)),
      button_(PushButton::create(pins.button)),
      config_button_(PushButton::create(pins.config_button)),
      run_button_(PushButton::create(pins.run_button)),
      mode_switch_(Switch::create(pins.switch_first, pins.switch_third)),
      wifi_(WifiRadio::create()) {}

esp_err_t Board::init_all() noexcept {
    if (esp_err_t err = chip_temperature_.init(); err != ESP_OK) {
        return err;
    }
    if (esp_err_t err = one_wire_.init(); err != ESP_OK) {
        return err;
    }
    if (esp_err_t err = green_.init(); err != ESP_OK) {
        return err;
    }
    if (esp_err_t err = red_.init(); err != ESP_OK) {
        return err;
    }
    if (esp_err_t err = button_.init(); err != ESP_OK) {
        return err;
    }
    if (esp_err_t err = config_button_.init(); err != ESP_OK) {
        return err;
    }
    if (esp_err_t err = run_button_.init(); err != ESP_OK) {
        return err;
    }
    if (esp_err_t err = mode_switch_.init(); err != ESP_OK) {
        return err;
    }
    return wifi_.init();
}

}  // namespace thermo::hal
