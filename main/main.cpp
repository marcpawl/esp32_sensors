// Firmware entry point.
//
// Reads the 3-position switch, initializes NVS + RTC state, then dispatches to
// Configure mode (implemented) or Run mode (stubbed) per §3.
#include <cstdio>

#include "app_config.hpp"
#include "app_config_store.hpp"
#include "configure_mode.hpp"
#include "constants.hpp"
#include "esp_err.h"
#include "esp_log.h"
#include "mode.hpp"
#include "mode_controller.hpp"
#include "nvs_flash.h"
#include "rtc_state.hpp"
#include "run_mode.hpp"

namespace {

constexpr const char* kTag = "app_main";

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

} // namespace

extern "C" void app_main(void) {
    ESP_ERROR_CHECK(init_nvs());
    thermo::rtc_state_init();

    thermo::ModeController mode_ctl(thermo::pins::kConfig, thermo::pins::kRun);
    ESP_ERROR_CHECK(mode_ctl.init());

    const thermo::Mode mode = mode_ctl.read();
    ESP_LOGI(kTag, "boot: mode=%s", thermo::to_string(mode));

    switch (mode) {
    case thermo::Mode::kConfigure:
        thermo::run_configure_mode();
        break;

    case thermo::Mode::kRun:
        // Run mode executes one cycle, then deep-sleeps (does not return).
        // If it does return (e.g. sleep disabled for debugging), loop.
        while (true) {
            thermo::run_mode_cycle();
        }
        break;

    case thermo::Mode::kOff:
        // The switch cuts power in the center position, so reaching here means
        // the sense pins read low transiently. Nothing to do; the device will
        // power down.
        ESP_LOGI(kTag, "Off position (no power path); idling");
        break;
    }
}
