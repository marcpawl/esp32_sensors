// Entry point for the `donny` linux-emulated application.
//
// Builds for the ESP-IDF `linux` target and runs natively on the host. It links
// the pin-free HAL (`hal_stub`), `espidf_cpp`, and `logic`: `app_main` builds
// the aggregate board, initializes it, then stays alive, sleeping for one
// minute per iteration.
// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#pragma GCC diagnostic pop

// NOLINTEND

#include <memory>

#include "hal/board_stub.hpp"
#include "hal/board_types.hpp"

namespace {

constexpr char kTag[] = "donny";
constexpr TickType_t kSpinPeriod = pdMS_TO_TICKS(60 * 1000);

}  // namespace

extern "C" void app_main(void) {
    ESP_LOGI(kTag, "donny running (linux emulation)");

    // The board is shared: it is heap-allocated and held by a `std::shared_ptr`
    // so it can be handed to the LED task (`logic/led_task.hpp` takes a
    // `std::shared_ptr<Board>` for any `hal::BoardLike` board and does not own
    // it) without the board being destroyed while the task still uses it.
    auto board = std::make_shared<thermo::hal::BoardStub>(
        thermo::hal::BoardStub::create({}));
    ESP_ERROR_CHECK(board->init_all());
    ESP_LOGI(kTag, "board initialized");

    while (true) {
        vTaskDelay(kSpinPeriod);
    }
}
