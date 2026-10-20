// Firmware entry point.
//
// Reads the 3-position switch, initializes NVS + RTC state, then dispatches to
// Configure mode (implemented) or Run mode (stubbed) per §3.
#include <cstdio>
#include <utility>

#include "app_config.hpp"
#include "app_config_store.hpp"
#include "configure_mode.hpp"
#include "constants.hpp"
#include "esp_err.h"
#include "esp_log.h"
#include "hal/board.hpp"
#include "hal/switch.hpp"
#include "logic/led_task.hpp"
#include "mode.hpp"
#include "nvs_flash.h"
#include "reboot.hpp"
#include "rtc_state.hpp"
#include "run_mode.hpp"

namespace {

constexpr const char* kTag = "app_main";

// Maps the raw switch pin states onto an operating mode (§3). A healthy
// 3-position ON-OFF-ON switch energizes at most one pole, so both-pins-on and
// both-pins-off are hardware faults: fail loudly rather than guess a mode.
thermo::Mode mode_from_pins(thermo::hal::Pins pins) {
    if (pins.first_on() && !pins.third_on()) {
        return thermo::Mode::kConfigure;
    }
    if (!pins.first_on() && pins.third_on()) {
        return thermo::Mode::kRun;
    }
    if (!pins.first_on() && !pins.third_on()) {
        return thermo::Mode::kOff;
    }
    // Both poles energized: the switch is miswired or shorted.
    ESP_LOGE(kTag, "switch fault: both sense pins on");
    thermo::reboot();
}

// Initializes NVS, erasing and retrying if the partition is full or had a
// version mismatch (standard ESP-IDF recovery for a corrupted NVS).
esp_err_t init_nvs() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(kTag, "NVS needs reinit (%s); erasing", esp_err_to_name(err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

}  // namespace

extern "C" void app_main(void) {
    ESP_ERROR_CHECK(init_nvs());
    thermo::rtc_state_init();

    thermo::hal::Switch mode_switch(thermo::pins::kConfig, thermo::pins::kRun);
    ESP_ERROR_CHECK(mode_switch.init());

    const thermo::Mode mode = mode_from_pins(mode_switch.read());
    ESP_LOGI(kTag, "boot: mode=%s", thermo::to_string(mode));

    // The LED task drives the LEDs through the board, so it is independent of
    // the concrete board implementation (hal_board on target, hal_stub under
    // QEMU/host tests). Bring the board up before starting the task.
    thermo::hal::Board board = thermo::hal::Board::create({});
    ESP_ERROR_CHECK(board.init_all());
    thermo::LedTask led_task(std::move(board));

    switch (mode) {
        case thermo::Mode::kConfigure:
            thermo::run_configure_mode(led_task);
            break;

        case thermo::Mode::kRun:
            // Run mode executes one cycle, then deep-sleeps (does not return).
            // If it does return (e.g. sleep disabled for debugging), loop.
            while (true) {
                thermo::run_mode_cycle(led_task);
            }
            break;

        case thermo::Mode::kOff:
            // The switch cuts power in the center position, so reaching here
            // means the sense pins read low transiently. Nothing to do; the
            // device will power down.
            ESP_LOGI(kTag, "Off position (no power path); idling");
            break;
    }
}
