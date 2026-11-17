// Deterministic, pin-free aggregate `BoardStub` for the host tests.
//
// Constructs one `hal_stub` device per board device and initializes them in a
// fixed order. Every stub `init()` is a no-op that succeeds, so `init_all()`
// always returns ESP_OK without touching hardware. Deep sleep (§3.8) is
// modelled in software as well: the request is recorded, the wake that would
// follow is armed, and the call returns, so a host test can assert both the
// duration and the wake the firmware reacted to.
// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#pragma GCC diagnostic pop
// NOLINTEND

#include <chrono>
#include <optional>

#include "hal_stub/board_stub.hpp"

// `esp_driver_gpio` registers an empty component on the `linux` target, so
// `driver/gpio.h` is not on the include path. The stub board still installs the
// ISR service like the hardware board; the symbol comes from `gpio_stub.cpp`.
extern "C" esp_err_t gpio_install_isr_service(int intr_alloc_flags);

namespace thermo::hal::stub {

BoardStub::BoardStub(BoardPins pins) noexcept
    : chip_temperature_(),
      one_wire_(OneWire::create(pins.one_wire)),
      green_(Led::create(pins.green_led, pins.green_name)),
      red_(Led::create(pins.red_led, pins.red_name)),
      user_switch_(Switch::create(pins.user_switch)),
      config_switch_(Switch::create(pins.config_switch)),
      run_switch_(Switch::create(pins.run_switch)),
      wifi_(WifiRadio::create()) {
    // Match the hardware board: switch handlers need the GPIO ISR service, so
    // the stub "installs" it too (a no-op on the host).
    (void)gpio_install_isr_service(0);
}

esp_err_t BoardStub::init_all() noexcept {
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
    if (esp_err_t err = user_switch_.init(); err != ESP_OK) {
        return err;
    }
    if (esp_err_t err = config_switch_.init(); err != ESP_OK) {
        return err;
    }
    if (esp_err_t err = run_switch_.init(); err != ESP_OK) {
        return err;
    }
    return wifi_.init();
}

void BoardStub::deep_sleep_start(std::chrono::seconds duration) noexcept {
    last_deep_sleep_ = duration;
    // The real board arms the RTC timer as its only wake source (§3.8), so the
    // boot that follows is a timer wake. Model that here, so a test can sleep
    // and then read back the wake the next cycle would see.
    wakeup_causes_ = wake_cause_bit(WakeCause::kTimer);
}

WakeCauses BoardStub::sleep_get_wakeup_causes() const noexcept {
    return wakeup_causes_;
}

void BoardStub::set_wakeup_causes(WakeCauses causes) noexcept {
    wakeup_causes_ = causes;
}

std::optional<std::chrono::seconds> BoardStub::last_deep_sleep()
    const noexcept {
    return last_deep_sleep_;
}

}  // namespace thermo::hal::stub
