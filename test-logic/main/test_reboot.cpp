// Tests for the reboot helper (`logic/reboot.hpp`).
//
// `thermo::reboot()` is `[[noreturn]]`: it waits for serial output to flush,
// then hands over to `esp_restart()`, which on the `linux` target runs the
// shutdown handlers and exits the process. The test registers a shutdown
// handler that longjmps back out of the restart, so it can observe both the
// flush delay and the restart path without ending the test runner.
// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#include "esp_system.h"
#pragma GCC diagnostic pop

// NOLINTEND

#include <chrono>

#include <csetjmp>

#include "logic/reboot.hpp"
#include "unity.h"

namespace {

std::jmp_buf g_reboot_jump;
bool g_shutdown_handler_ran = false;

// Registered as a shutdown handler: `esp_restart()` calls it before tearing
// the system down, and the longjmp returns control to the test case.
void jump_back_from_restart() {
    g_shutdown_handler_ran = true;
    std::longjmp(g_reboot_jump, 1);
}

}  // namespace

TEST_CASE("reboot: waits for the log flush, then enters the restart path",
          "[reboot]") {
    TEST_ASSERT_EQUAL(ESP_OK,
                      esp_register_shutdown_handler(jump_back_from_restart));

    g_shutdown_handler_ran = false;
    const auto start = std::chrono::steady_clock::now();
    bool returned = false;

    if (setjmp(g_reboot_jump) == 0) {
        thermo::reboot();
        // Reached only if the reboot helper returned, which its `[[noreturn]]`
        // contract forbids.
        returned = true;
    }

    const auto elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start)
            .count();

    TEST_ASSERT_EQUAL(ESP_OK,
                      esp_unregister_shutdown_handler(jump_back_from_restart));

    TEST_ASSERT_FALSE_MESSAGE(returned, "reboot() returned");
    TEST_ASSERT_TRUE_MESSAGE(g_shutdown_handler_ran,
                             "reboot() did not enter the restart path");

    // reboot() waits 2 s (pdMS_TO_TICKS(2000) at this project's 100 Hz tick)
    // before restarting so serial logging can drain. A tick-aligned delay may
    // be one tick short, so 1900 ms is the floor.
    TEST_ASSERT_GREATER_OR_EQUAL_INT(1900, static_cast<int>(elapsed_ms));
}
