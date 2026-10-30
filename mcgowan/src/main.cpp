// Firmware entry point.
//
// Reads the mode-selection buttons, initializes NVS + RTC state, then
// dispatches to Configure mode (implemented) or Run mode (stubbed) per §3.
// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"
#pragma GCC diagnostic pop
// NOLINTEND

#include <memory>

#include <cstdio>

#include "app_config.hpp"
#include "app_config_store.hpp"
#include "configure_mode.hpp"
#include "hal/board.hpp"
#include "hal/push_button.hpp"
#include "logic/led_task.hpp"
#include "logic/mode.hpp"
#include "logic/mode_selection.hpp"
#include "reboot.hpp"
#include "rtc_storage.hpp"
#include "run_mode.hpp"

namespace {

constexpr const char* kTag = "app_main";

// Reads the two mode-sense poles and maps them to a mode (§3). The mapping
// itself is pure and host-tested (`logic/mode_selection.hpp`); this wrapper
// only turns its hardware-fault result into the firmware's fail-loudly policy.
thermo::Mode read_mode(thermo::hal::Board& board) {
    const thermo::ModeSelection selection = thermo::mode_from_buttons(
        board.config_button().read(), board.run_button().read());
    if (!selection.has_value()) {
        // Both poles energized: the switch is miswired or shorted. Fail loudly
        // rather than guess a mode.
        ESP_LOGE(kTag, "switch fault: both sense pins on");
        thermo::reboot();
    }
    return *selection;
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

    // The board is created first because both the boot policy and the
    // mode-sense buttons go through it: `rtc_state_init()` asks it why the last
    // wake happened (§3.8) and `read_mode()` reads the switch poles. It is
    // shared: `LedTask` holds a `std::shared_ptr` to it instead of owning it,
    // so it cannot be destroyed while the task still uses it.
    auto board =
        std::make_shared<thermo::hal::Board>(thermo::hal::Board::create({}));
    ESP_ERROR_CHECK(board->init_all());

    // Cold boot clears the retained block; a deep-sleep wake keeps it (§8).
    thermo::rtc_state_init(*board);

    const thermo::Mode mode = read_mode(*board);
    ESP_LOGI(kTag, "boot: mode=%s", thermo::to_string(mode));

    // The LED task drives the LEDs through the board, so it is independent of
    // the concrete board implementation (hal_board on target, hal_stub in the
    // host tests).
    thermo::LedTask led_task(board);

    switch (mode) {
        case thermo::Mode::kConfigure:
            thermo::run_configure_mode(led_task);
            break;

        case thermo::Mode::kRun:
            // Run mode executes one cycle, then deep-sleeps (does not return).
            // If it does return (e.g. sleep disabled for debugging), loop.
            while (true) {
                thermo::run_mode_cycle(*board, led_task);
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
