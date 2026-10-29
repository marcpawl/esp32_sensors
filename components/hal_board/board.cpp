// Real, GPIO-backed aggregate Board for the dev board. Constructs each device
// from the wiring in `BoardPins` and initializes them in a fixed order.
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
    return wifi_.init();
}

}  // namespace thermo::hal
