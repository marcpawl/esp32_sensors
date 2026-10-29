// Real, GPIO-backed aggregate Board for the dev board. Constructs each device
// from the wiring in `BoardPins` and initializes them in a fixed order. Also
// owns deep sleep (§3.8), which is a property of the board rather than of a
// peripheral.
// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#include "esp_sleep.h"
#pragma GCC diagnostic pop
// NOLINTEND

#include <chrono>

#include <cstdint>

#include "hal/board.hpp"

namespace thermo::hal {
namespace {

// `WakeCause` names ESP-IDF's `esp_sleep_source_t` values on purpose, so the
// hardware board can pass the wake-cause bitmap straight through. Pin that
// contract down: a future ESP-IDF that renumbers the enum must fail the build
// here rather than silently misreport a wake.
static_assert(static_cast<std::uint32_t>(WakeCause::kUndefined) ==
              ESP_SLEEP_WAKEUP_UNDEFINED);
static_assert(static_cast<std::uint32_t>(WakeCause::kTimer) ==
              ESP_SLEEP_WAKEUP_TIMER);

}  // namespace

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

void Board::deep_sleep_start(std::chrono::seconds duration) noexcept {
    const auto micros =
        std::chrono::duration_cast<std::chrono::microseconds>(duration);
    // §3.8 is exactly this pair of calls; the timer is the only wake source
    // this firmware uses, so enabling it does not fight other sources.
    esp_sleep_enable_timer_wakeup(static_cast<std::uint64_t>(micros.count()));
    esp_deep_sleep_start();
}

WakeCauses Board::sleep_get_wakeup_causes() const noexcept {
    return esp_sleep_get_wakeup_causes();
}

}  // namespace thermo::hal
