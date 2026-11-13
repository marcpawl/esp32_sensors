// Entry point for the `donny` linux-emulated application.
//
// Builds for the ESP-IDF `linux` target and runs natively on the host. It links
// the pin-free HAL (`hal_stub`), `libespidfcpp`, and `liblogic`.
//
// Boot order matters: the interactive console task (`console_task.hpp`) starts
// *before* the `Application` is constructed, so a test driver can already drive
// the stub board (press a button, set a temperature) while the application
// boots, and can keep doing so while it runs.
// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#pragma GCC diagnostic pop

// NOLINTEND

#include <memory>

#include <cstdio>

#include "console_task.hpp"
#include "hal/board_stub.hpp"
#include "hal/board_types.hpp"
#include "logic/application.hpp"

namespace {

constexpr char kTag[] = "donny";

// Sends ESP-IDF's log output to stderr.
//
// The console task's protocol owns stdout (one JSON document per line), but
// ESP-IDF logs to stdout by default (`esp_log_vprint_func = &vprintf`), and the
// stub devices log whenever they are written to. Without this, applying a line
// that sets an LED would interleave a "Green: On" log line with the protocol
// output. Diagnostics stay readable on stderr; the protocol stays parseable.
int log_to_stderr(const char* format, va_list args) {
    return std::vfprintf(stderr, format, args);
}

// Installs the redirect at static-initialization time, before the scheduler
// starts. Doing it inside `app_main` is too late: the port layer logs
// "Starting scheduler" first, which would land on stdout and prepend a stray
// line to the protocol stream.
struct LogRedirect {
    LogRedirect() noexcept { esp_log_set_vprintf(&log_to_stderr); }
};

[[maybe_unused]] const LogRedirect kLogRedirect{};

}  // namespace

extern "C" void app_main(void) {
    ESP_LOGI(kTag, "donny running (linux emulation)");

    // The board is shared: it is heap-allocated and held by a `std::shared_ptr`
    // so it can be handed to the console task and the application
    // (`logic/led_task.hpp` takes a `std::shared_ptr<Board>` for any
    // `hal::BoardLike` board and does not own it) without the board being
    // destroyed while either still uses it.
    auto board = std::make_shared<thermo::hal::BoardStub>(
        thermo::hal::BoardStub::create({}));
    ESP_ERROR_CHECK(board->init_all());
    ESP_LOGI(kTag, "board initialized");

    // Started before the application, so a driver can set up the board while
    // the application boots.
    thermo::ConsoleTask console(board);

    thermo::Application<thermo::hal::BoardStub> app(board);
    app.application_main();
}
